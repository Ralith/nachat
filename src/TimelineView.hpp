#ifndef NATIVE_CHAT_TIMELINE_VIEW_HPP_
#define NATIVE_CHAT_TIMELINE_VIEW_HPP_

#include <deque>
#include <experimental/optional>
#include <chrono>

#include <QAbstractScrollArea>
#include <QTextLayout>
#include <QPixmap>

#include <span.h>

#include "matrix/Event.hpp"

#include "ContentCache.hpp"
#include "FixedVector.hpp"

class QEvent;
class QShortcut;

namespace matrix {
class RoomState;
}

struct Selection {
  QPointF begin, end;
};

using Time = std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>;

struct EventLike {
  struct MemberInfo {
    matrix::UserID user;
    matrix::event::room::MemberContent prev_content;
  };

  std::experimental::optional<matrix::event::Room> event;
  matrix::EventType type;
  std::experimental::optional<Time> time;
  matrix::UserID sender;
  std::experimental::optional<QString> disambiguation;

  std::experimental::optional<matrix::event::room::MemberContent> member_content;
  // Set to sender's info iff sender is a member of the room

  std::experimental::optional<MemberInfo> affected_user_info;
  // Set to information about the affected user iff type == Member::tag()

  std::experimental::optional<matrix::EventID> redacts;
  // Set to the redacted event iff type == Redaction::tag()

  matrix::event::Content content;

  explicit EventLike(const matrix::RoomState &, matrix::event::Room real);
  EventLike(const matrix::RoomState &, const matrix::UserID &sender, Time time, matrix::EventType type, matrix::event::Content content,
            std::experimental::optional<matrix::UserID> affected_user = {}, std::experimental::optional<matrix::EventID> redacts = {});

  std::experimental::optional<matrix::event::room::MemberContent> effective_profile() const;
  void redact(const matrix::event::room::Redaction &);
};

class TimelineView;

class EventBlock {
public:
  EventBlock(TimelineView &parent, ThumbnailCache &cache, gsl::span<const EventLike *const> events); // All events should have same sender

  void update_layout(qreal width);

  QRectF bounds() const;
  void draw(QPainter &painter, Selection *selection = nullptr) const;
  void handle_input(const QPointF &point, QEvent *input);

private:
  struct Event {
    std::experimental::optional<matrix::event::Room> source;
    FixedVector<QTextLayout> paragraphs;

    void init(const TimelineView &, const EventBlock &, const EventLike &);
  };

  struct TimeInfo {
    Time start, end;
  };

  friend struct Event;

  TimelineView &parent_;
  matrix::UserID sender_;
  std::experimental::optional<ThumbnailRef> avatar_;
  QTextLayout name_, timestamp_;
  std::experimental::optional<TimeInfo> time_;
  FixedVector<Event> events_;

  qreal avatar_extent() const;
  qreal horizontal_padding() const;
};

class TimelineView : public QAbstractScrollArea {
  Q_OBJECT

public:
  TimelineView(ThumbnailCache &cache, QWidget *parent = nullptr);

  void prepend(const matrix::TimelineCursor &begin, const matrix::RoomState &state, const matrix::event::Room &evt);
  void append(const matrix::TimelineCursor &begin, const matrix::RoomState &state, const matrix::event::Room &evt);
  void redact(const matrix::event::room::Redaction &); // Call only on redaction events received from sync

  void add_pending(const QString &transaction, const matrix::RoomState &state, const matrix::UserID &self, Time time,
                   matrix::EventType type, matrix::event::Content content, std::experimental::optional<matrix::UserID> member_target = {});

  void set_at_bottom(bool);

signals:
  void need_backwards();
  void need_forwards();

  void redact_requested(const matrix::EventID &id, const QString &reason);
  void event_read(const matrix::EventID &id);

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void focusOutEvent(QFocusEvent *event) override;
  void contextMenuEvent(QContextMenuEvent *event) override;
  bool viewportEvent(QEvent *event) override;
  void changeEvent(QEvent *event) override;

private:
  struct Batch {
    matrix::TimelineCursor begin;
    std::deque<EventLike> events;

    Batch(matrix::TimelineCursor begin, std::deque<EventLike> events) : begin{std::move(begin)}, events{std::move(events)} {}
  };

  struct Pending {
    QString transaction;
    EventLike event;
  };

  ThumbnailCache &thumbnail_cache_;
  std::deque<Pending> pending_;
  std::deque<Batch> batches_;
  std::deque<EventBlock> blocks_;
  Selection selection_;
  QShortcut *copy_;
  QPixmap spinner_;
  bool at_bottom_;

  QString selection_text() const;
  void copy() const;
  QRectF view_rect() const;     // in coordinate space such that (0,0) = bottom-left of latest message
  void update_scrollbar(int content_height);
  void rebuild_blocks();
  void update_layout();
  void maybe_need_backwards();
  void maybe_need_forwards();
  bool at_top() const;
  qreal spinner_space() const;
  void draw_spinner(QPainter &painter, qreal top) const;
};

#endif
