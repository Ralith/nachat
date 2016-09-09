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

TimelineWindow::TimelineWindow(const std::deque<Batch> &batches, const RoomState &final_state)
  : initial_state_{final_state}, final_state_{final_state},
    batches_(batches.begin(), batches.empty() ? throw std::invalid_argument("timeline window must be construct from at least one batch") : batches.end()-1),
    latest_batch_{*(batches.end()-1)}
{
  if(!batches_.empty()) {
    batches_end_ = latest_batch_.begin;
  }
  for(auto it = batches.crbegin(); it != batches.crend(); ++it) {
    revert_batch(initial_state_, *it);
  }
}

void TimelineWindow::discard(const TimelineCursor &batch, Direction dir) {
  if(dir == Direction::FORWARD) {
    revert_batch(final_state_, latest_batch_);
    for(auto it = batches_.crbegin(); it != batches_.crend(); ++it) {
      revert_batch(final_state_, *it);
      if(it->begin == batch) {
        if(it.base() != batches_.begin()) {
          batches_end_ = it->begin;
        } else {
          batches_end_ = {};
        }
        batches_.erase(it.base(), batches_.end());
        return;
      }
    }
  } else {
    for(auto it = batches_.cbegin(); it != batches_.cend(); ++it) {
      for(const auto &evt : it->events) {
        if(auto s = evt.to_state()) initial_state_.apply(*s);
      }
      if(it->begin == batch) {
        batches_.erase(batches_.cbegin(), it);
        return;
      }
    }
    if(batch == latest_batch_.begin) {
      batches_.clear();
      return;
    }
  }
  qDebug() << "timeline window tried to discard unknown batch";
  batches_.clear();
}

bool TimelineWindow::at_start() const {
  return (!batches_.empty() && batches_.back().events.back().type() == event::room::Create::tag())
    || (!latest_batch_.events.empty() && latest_batch_.events.back().type() == event::room::Create::tag());
}

bool TimelineWindow::at_end() const {
  return batches_.empty() || *batches_end_ == latest_batch_.begin;
}

void TimelineWindow::append_batch(const TimelineCursor &batch_start, const TimelineCursor &batch_end, gsl::span<const event::Room> events,
                                  TimelineManager *mgr) {
  if(!end() || batch_start != *this->end()) {
    if(end()) mgr->grow(Direction::FORWARD);
    return;
  }
  if(events.empty()) return;

  batches_.emplace_back(batch_start, std::vector<event::Room>(events.begin(), events.end()));
  batches_end_ = batch_end;

  for(const auto &evt : batches_.back().events) {
    mgr->grew(Direction::FORWARD, batch_start, final_state_, evt);
    if(auto s = evt.to_state()) {
      final_state_.apply(*s);
    }
  }

  if(latest_batch_.begin == batch_end) {
    for(const auto &evt : latest_batch_.events) {
      mgr->grew(Direction::FORWARD, batch_start, final_state_, evt);
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
  if(batches_.size() == 1) {
    batches_end_ = batch_start;
  }

  for(auto it = batches_.front().events.crbegin(); it != batches_.front().events.crend(); ++it) {
    if(auto s = it->to_state()) initial_state_.revert(*s);
    mgr->grew(Direction::BACKWARD, batch_start, initial_state_, *it);
  }
}

void TimelineWindow::append_sync(const proto::Timeline &t, TimelineManager *mgr) {
  if(t.events.empty()) return;

  if(at_end() && !t.limited) {  // FIXME: Don't discard previous batch in the event of a limited sync
    if(!latest_batch_.events.empty()) batches_.emplace_back(std::move(latest_batch_));
    batches_end_ = t.prev_batch;
  }

  latest_batch_ = Batch{t.prev_batch, t.events};
  
  if(t.limited) mgr->discontinuity();

  if(at_end()) {
    assert(!t.limited);
    for(const auto &evt : latest_batch_.events) {
      mgr->grew(Direction::FORWARD, latest_batch_.begin, final_state_, evt);
      if(auto s = evt.to_state()) {
        final_state_.apply(*s);
      }
    }
  }
}

void TimelineWindow::reset(const RoomState &current_state) {
  batches_.clear();
  batches_end_ = {};
  final_state_ = current_state;
  initial_state_ = final_state_;
  revert_batch(initial_state_, latest_batch_);
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
    end = window_.latest_begin();
  } else {
    if(backward_req_ || window_.at_start()) return;
    start = window_.begin();
  }

  if(!start) return;            // throw?
  auto reply = room_.get_messages(dir, *start, BATCH_SIZE, end);
  qDebug() << "requesting" << (dir == Direction::BACKWARD ? "backward" : "forward") << "from" << start->value() << "to" << (end ? end->value() : "infinity");

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
  if(window().at_end()) {
    for(const auto &evt : window().latest_batch().events) {
      grew(Direction::FORWARD, window().latest_batch().begin, replay, evt);
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
  qDebug() << "got batch of" << reversed_events.size() << "events from" << start.value() << "to" << end.value();
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
