#include "TimelineWindow.hpp"

#include <QDebug>
#include <stdexcept>

#include "Room.hpp"
#include "proto.hpp"

using std::experimental::optional;

namespace matrix {

namespace {

constexpr size_t BATCH_SIZE = 50;

void revert_batch(RoomState &state, const Batch &batch) {
  for(auto it = batch.events.crbegin(); it != batch.events.crend(); ++it) {
    if(auto s = it->to_state()) state.revert(*s);
  }
}

}

TimelineWindow::TimelineWindow(std::deque<Batch> batches, const RoomState &final_state)
  : initial_state_{final_state}, final_state_{final_state},
    batches_{std::move(batches)},
    sync_batch_{batches_.empty() ? throw std::invalid_argument("timeline window must be construct from at least one batch") : batches_.back()}
{
  for(auto it = batches_.crbegin(); it != batches_.crend(); ++it) {
    revert_batch(initial_state_, *it);
  }
}

void TimelineWindow::discard(const TimelineCursor &batch, Direction dir) {
  if(dir == Direction::FORWARD) {
    for(auto it = batches_.crbegin(); it != batches_.crend(); ++it) {
      if(it->begin == batch) {
        if(it.base() != batches_.cend()) {
          batches_end_ = it.base()->begin;
        }
        batches_.erase(it.base(), batches_.end());
        return;
      }
      revert_batch(final_state_, *it);
    }
  } else {
    for(auto it = batches_.cbegin(); it != batches_.cend(); ++it) {
      if(it->begin == batch) {
        batches_.erase(batches_.cbegin(), it);
        return;
      }
      for(const auto &evt : it->events) {
        if(auto s = evt.to_state()) initial_state_.apply(*s);
      }
    }
  }
  qDebug() << "timeline window tried to discard unknown batch" << batch.value();
  batches_.clear();
}

bool TimelineWindow::at_start() const {
  return batches_.front().events.front().type() == event::room::Create::tag();
}

bool TimelineWindow::at_end() const {
  return batches_.empty() || sync_batch_.begin == batches_.back().begin;
}

void TimelineWindow::append_batch(const TimelineCursor &batch_start, const TimelineCursor &batch_end, gsl::span<const event::Room> events,
                                  TimelineManager *mgr) {
  if(!end() || batch_start != *this->end()) {
    if(end()) mgr->grow(Direction::FORWARD);
    return;
  }

  size_t new_batches = 0;
  if(!events.empty()) {
    batches_.emplace_back(batch_start, std::vector<event::Room>(events.begin(), events.end()));
    batches_end_ = batch_end;
    ++new_batches;
  }

  if(static_cast<size_t>(events.size()) < BATCH_SIZE) {
    batches_.emplace_back(sync_batch_);
    batches_end_ = {};
    ++new_batches;
  }

  for(size_t i = 0; i < new_batches; ++i) {
    for(const auto &evt : batches_[batches_.size()-new_batches+i].events) {
      mgr->grew(Direction::FORWARD, batches_.back().begin, final_state_, evt);
      if(auto s = evt.to_state()) {
        final_state_.apply(*s);
      }
    }
  }
}

void TimelineWindow::prepend_batch(const TimelineCursor &batch_start, const TimelineCursor &batch_end, gsl::span<const event::Room> reversed_events,
                                   TimelineManager *mgr) {
  if(batch_start != begin()) {  // we compare begin to begin here because start/end are reversed for backwards fetches
    mgr->grow(Direction::BACKWARD);
    return;
  }

  if(reversed_events.empty()) return;

  batches_.emplace_front(batch_end, std::vector<event::Room>(reversed_events.rbegin(), reversed_events.rend()));

  for(auto it = batches_.front().events.crbegin(); it != batches_.front().events.crend(); ++it) {
    if(auto s = it->to_state()) initial_state_.revert(*s);
    mgr->grew(Direction::BACKWARD, batch_start, initial_state_, *it);
  }
}

void TimelineWindow::append_sync(const proto::Timeline &t, TimelineManager *mgr) {
  if(t.events.empty()) return;

  if(at_end()) {
    if(t.limited) {
      batches_.clear();         // FIXME: Don't nuke history
    }
    batches_.emplace_back(t.prev_batch, t.events);
  }

  sync_batch_ = Batch{t.prev_batch, t.events};

  if(at_end()) {
    if(t.limited) {
      mgr->discontinuity();
    }

    for(const auto &evt : sync_batch_.events) {
      mgr->grew(Direction::FORWARD, sync_batch_.begin, final_state_, evt);
      if(auto s = evt.to_state()) {
        final_state_.apply(*s);
      }
    }
  }
}

void TimelineWindow::reset(const RoomState &current_state) {
  batches_.clear();
  batches_.emplace_back(sync_batch_);
  batches_end_ = {};
  final_state_ = current_state;
  initial_state_ = current_state;
  revert_batch(initial_state_, sync_batch_);
}


TimelineManager::TimelineManager(Room &room, QObject *parent)
  : QObject(parent), room_(room), window_{room.buffer(), room.state()}, forward_req_{nullptr}, backward_req_{nullptr}
{
  retry_timer_.setSingleShot(true);
  retry_timer_.setInterval(1000);
  connect(&retry_timer_, &QTimer::timeout, this, &TimelineManager::retry);
  connect(&room, &Room::sync_complete, this, &TimelineManager::batch);
}

void TimelineManager::grow(Direction dir) {
  optional<TimelineCursor> start, end;
  if(dir == Direction::FORWARD) {
    if(forward_req_ || window_.at_end()) return;
    start = window_.end();
    end = window_.sync_begin();
  } else {
    if(backward_req_ || window_.at_start()) return;
    start = window_.begin();
  }

  if(!start) {
    throw std::logic_error("tried to grow from an undefined cursor");
  }
  auto reply = room_.get_messages(dir, *start, BATCH_SIZE, end);

  if(dir == Direction::FORWARD) {
    connect(reply, &MessageFetch::finished, this, &TimelineManager::got_forward);
    connect(reply, &MessageFetch::error, this, &TimelineManager::forward_fetch_error);
    forward_req_ = reply;
  } else {
    connect(reply, &MessageFetch::finished, this, &TimelineManager::got_backward);
    connect(reply, &MessageFetch::error, this, &TimelineManager::backward_fetch_error);
    backward_req_ = reply;
  }
}

void TimelineManager::replay() {
  auto replay = window().initial_state();
  for(const auto &batch : window().batches()) {
    for(const auto &evt : batch.events) {
      grew(Direction::FORWARD, batch.begin, replay, evt);
      if(auto s = evt.to_state()) replay.apply(*s);
    }
  }
}

void TimelineManager::retry() {
  grow(retry_dir_);
}

void TimelineManager::forward_fetch_error(const QString &msg) {
  forward_req_ = nullptr;
  error(Direction::BACKWARD, msg);
}

void TimelineManager::backward_fetch_error(const QString &msg) {
  backward_req_ = nullptr;
  error(Direction::FORWARD, msg);
}

void TimelineManager::error(Direction dir, const QString &msg) {
  // TODO: Check whether error is retry-worthy?
  qWarning() << room_.pretty_name() << "retrying timeline fetch due to error:" << msg;
  retry_dir_ = dir;
  if(!retry_timer_.isActive()) retry_timer_.start();
}

void TimelineManager::got_backward(const TimelineCursor &start, const TimelineCursor &end, gsl::span<const event::Room> reversed_events) {
  backward_req_ = nullptr;
  window_.prepend_batch(start, end, reversed_events, this);
}

void TimelineManager::got_forward(const TimelineCursor &start, const TimelineCursor &end, gsl::span<const event::Room> events) {
  forward_req_ = nullptr;
  window_.append_batch(start, end, events, this);
}

void TimelineManager::batch(const proto::Timeline &t) {
  window_.append_sync(t, this);
}

}
