#ifndef NATIVE_CHAT_MATRIX_TIMELINE_WINDOW_HPP_
#define NATIVE_CHAT_MATRIX_TIMELINE_WINDOW_HPP_

#include <deque>
#include <vector>
#include <experimental/optional>

#include <QObject>
#include <QTimer>

#include <span.h>

#include "Event.hpp"
#include "Room.hpp"

namespace matrix {

namespace proto {
struct Timeline;
}

class TimelineManager;

class TimelineWindow {
public:
  TimelineWindow(const RoomState &initial_state, gsl::span<const Batch> batches, const RoomState &final_state);

  void discard(const TimelineCursor &, Direction dir);

  bool at_start() const;
  bool at_end() const;

  TimelineCursor begin() const {
    if(batches_.empty()) {
      return latest_batch_.begin;
    }
    return batches_.front().begin;
  }

  // Empty => window includes present
  std::experimental::optional<TimelineCursor> end() const {
    if(batches_end_ && *batches_end_ == latest_batch_.begin) return {};
    return batches_end_;
  }

  TimelineCursor latest_begin() const {
    return latest_batch_.begin;
  }

  void prepend_batch(const TimelineCursor &start, const TimelineCursor &end, gsl::span<const event::Room> reversed_events,
                     TimelineManager *mgr);
  void append_batch(const TimelineCursor &start, const TimelineCursor &end, gsl::span<const event::Room> events,
                    TimelineManager *mgr);
  void append_sync(const proto::Timeline &t, TimelineManager *mgr);

  void reset(const RoomState &current_state);
  // Discard all but latest

  const RoomState &initial_state() { return initial_state_; }
  const std::deque<Batch> &batches() const { return batches_; }
  const Batch &latest_batch() const { return latest_batch_; }
  const RoomState &final_state() { return final_state_; }

private:
  RoomState initial_state_, final_state_;
  std::deque<Batch> batches_;   // have nonempty events
  std::experimental::optional<TimelineCursor> batches_end_;
  Batch latest_batch_;
};

class TimelineManager : public QObject {
  Q_OBJECT

public:
  explicit TimelineManager(Room &room, QObject *parent = nullptr);

  TimelineWindow &window() { return window_; }
  const TimelineWindow &window() const { return window_; }

  void grow(Direction dir);

  void replay();

signals:
  void grew(Direction dir, const TimelineCursor &begin, const RoomState &state, const event::Room &evt);

  void discontinuity();
  // Gap between successive syncs; if latest batch is being displayed, user should discard it and proceed as if paging
  // forwards in time

private:
  Room &room_;
  TimelineWindow window_;

  MessageFetch *forward_req_;
  MessageFetch *backward_req_;

  QTimer retry_timer_;
  Direction retry_dir_;

  // Helpers
  void retry();
  void error(Direction dir, const QString &msg);

  // Signal handlers
  void forward_fetch_error(const QString &msg);
  void backward_fetch_error(const QString &msg);
  void got_backward(const TimelineCursor &start, const TimelineCursor &end, gsl::span<const event::Room> reversed_events);
  void got_forward(const TimelineCursor &start, const TimelineCursor &end, gsl::span<const event::Room> events);
  void batch(const proto::Timeline &t);
};

}

#endif
