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

using Time = std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>;

class TimelineEventID : public matrix::ID<uint64_t> { using ID::ID; };

struct EventLike {
  struct MemberInfo {
    matrix::UserID user;
    matrix::event::room::MemberContent prev_content;
  };

  TimelineEventID id;
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

  explicit EventLike(TimelineEventID id, const matrix::RoomState &, matrix::event::Room real);
  EventLike(TimelineEventID id, const matrix::RoomState &,
            const matrix::UserID &sender, Time time, matrix::EventType type, matrix::event::Content content,
            std::experimental::optional<matrix::UserID> affected_user = {}, std::experimental::optional<matrix::EventID> redacts = {});

  std::experimental::optional<matrix::event::room::MemberContent> effective_profile() const;
  void redact(const matrix::event::room::Redaction &);
};

class Cursor {
public:
  enum class Type {
    NAME, TIMESTAMP, BODY
  };

  Cursor(Type ty, TimelineEventID event, size_t paragraph, int pos) : type_{ty}, event_{event}, paragraph_{paragraph}, pos_{pos} {}
  Cursor(Type ty, TimelineEventID event, int pos) : Cursor{ty, event, 0, pos} {}
  Cursor(TimelineEventID event, size_t paragraph, int pos) : Cursor{Type::BODY, event, paragraph, pos} {}

  Type type() const { return type_; }
  size_t paragraph() const { return paragraph_; } // 0 for type NAME or TIMESTAMP
  int pos() const { return pos_; }
  
  TimelineEventID event() const { return event_; }
  // For type NAME or TIMESTAMP, first event in a block

private:
  Type type_;
  TimelineEventID event_;
  size_t paragraph_;
  int pos_;
};

inline bool operator==(const Cursor &a, const Cursor &b) {
  return a.type() == b.type() && a.pos() == b.pos() && a.event() == b.event() && a.paragraph() == b.paragraph();
}
inline bool operator!=(const Cursor &a, const Cursor &b) { return !(a == b); }

struct Selection {
  enum class Mode {
    CHARACTER, WORD, PARAGRAPH
  };

  Mode mode;
  Cursor begin;
  Cursor end;

  Selection() : mode{Mode::CHARACTER}, begin{TimelineEventID{0}, 0, 0}, end{begin} {}
};

class TimelineView;

struct CursorWithHref {
  Cursor cursor;
  std::experimental::optional<QString> href;
};

class EventBlock {
public:
  EventBlock(TimelineView &parent, ThumbnailCache &cache, gsl::span<const EventLike *const> events); // All events should have same sender

  void update_layout(qreal width);

  QRectF bounds() const;
  bool draw(QPainter &painter, bool bottom_selected, const Selection &selection) const; // returns true iff selection began but did not end
  void handle_input(const QPointF &point, QEvent *input);

  std::experimental::optional<CursorWithHref> get_cursor(const QPointF &, bool exact = false) const;

  struct SelectionTextResult {
    QString fragment;
    bool continues;
  };

  SelectionTextResult selection_text(bool bottom_selected, const Selection &selection) const;

  bool has(TimelineEventID event) const;

private:
  struct Event {
    TimelineEventID id;
    matrix::EventType type;
    std::experimental::optional<Time> time;
    std::experimental::optional<matrix::event::Room> source;
    FixedVector<QTextLayout> paragraphs;

    Event(const TimelineView &, const EventBlock &, const EventLike &);
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
  const Event *event_at(const QPointF &) const;
};

class TimelineView : public QAbstractScrollArea {
  Q_OBJECT

public:
  TimelineView(const QUrl &homeserver, ThumbnailCache &cache, QWidget *parent = nullptr);

  void prepend(const matrix::TimelineCursor &begin, const matrix::RoomState &state, const matrix::event::Room &evt);
  void append(const matrix::TimelineCursor &begin, const matrix::RoomState &state, const matrix::event::Room &evt);
  void redact(const matrix::event::room::Redaction &); // Call only on redaction events received from sync

  // FIXME: Pending messages should always be rendered according to the most recent room state, not the one when they were sent.
  void add_pending(const matrix::TransactionID &transaction, const matrix::RoomState &state, const matrix::UserID &self, Time time,
                   matrix::EventType type, matrix::event::Content content, std::experimental::optional<matrix::UserID> affected_user = {});

  void set_at_bottom(bool);

  const QUrl &homeserver() const { return homeserver_; }

signals:
  void need_backwards();
  void need_forwards();

  void redact_requested(const matrix::EventID &id, const QString &reason);
  void event_read(const matrix::EventID &id);
  void view_user_profile(const matrix::UserID &user);

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
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
    matrix::TransactionID transaction;
    EventLike event;

    Pending(const matrix::TransactionID &tx, EventLike e) : transaction{tx}, event{std::move(e)} {}
  };

  struct Position {
    TimelineEventID event;
    qreal from_bottom;
  };

  class VisibleBlock {
  public:
    VisibleBlock(EventBlock &block, const QPointF &origin) : block_{block}, origin_{origin} {}

    EventBlock &block() { return block_; }
    const EventBlock &block() const { return block_; }
    QRectF bounds() const;

  private:
    EventBlock &block_;
    QPointF origin_;
  };

  QUrl homeserver_;
  ThumbnailCache &thumbnail_cache_;
  std::deque<Pending> pending_;
  std::deque<Batch> batches_;
  std::deque<EventBlock> blocks_;
  std::vector<VisibleBlock> visible_blocks_;
  Selection selection_;
  bool selection_updating_;
  std::chrono::steady_clock::time_point last_click_;
  size_t click_count_;
  QShortcut *copy_;
  QPixmap spinner_;
  bool at_bottom_;
  uint64_t id_counter_;
  std::experimental::optional<Position> scroll_position_;

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
  void dispatch_input(const QPointF &point, QEvent *input);
  TimelineEventID get_id();
  std::experimental::optional<Cursor> get_cursor(const QPointF &point, bool exact) const;
};

#endif
