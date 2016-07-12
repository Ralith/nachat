#ifndef NATIVE_CHAT_TIMELINE_VIEW_HPP_
#define NATIVE_CHAT_TIMELINE_VIEW_HPP_

#include <deque>
#include <chrono>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <experimental/optional>

#include <QAbstractScrollArea>
#include <QTextLayout>
#include <QIcon>

#include "matrix/Event.hpp"
#include "matrix/Room.hpp"
#include "matrix/Content.hpp"

#include "QStringHash.hpp"

class TimelineView : public QAbstractScrollArea {
  Q_OBJECT

public:
  TimelineView(matrix::Room &room, QWidget *parent = nullptr);

  void end_batch(const QString &token);
  // Call upon receiving a new batch, with that batch's prev_batch token.

  void push_back(const matrix::RoomState &state, const matrix::proto::Event &e);

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  struct Event {
    std::vector<QTextLayout> layouts;
    const std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds> time;

    Event(const TimelineView &, const matrix::proto::Event &);
    QRectF bounding_rect() const;
    void update_layout(const TimelineView &);
  };

  class Block {
  public:
    Block(TimelineView &, const matrix::RoomState &, const matrix::proto::Event &, Event &);
    void update_layout(const TimelineView &);
    void draw(const TimelineView &view, QPainter &p, QPointF offset) const;
    QRectF bounding_rect(const TimelineView &view) const;

    const QString &sender_id() const { return sender_id_; }
    size_t size() const;
    const std::experimental::optional<matrix::Content> &avatar() const { return avatar_; }

    std::deque<Event *> &events() { return events_; }
    const std::deque<Event *> &events() const { return events_; }

  private:
    const QString event_id_;
    const QString sender_id_;
    std::experimental::optional<matrix::Content> avatar_;

    QTextLayout name_layout_, timestamp_layout_;

    std::deque<Event *> events_;  // deque so we can add events to either end
  };

  struct Batch {
    std::deque<Event> events;  // deque purely so we don't try to copy/move QTextLayout
    QString token;

    size_t size() const;
  };

  struct Avatar {
    size_t references = 0;
    QPixmap pixmap;
  };

  matrix::Room &room_;
  matrix::RoomState initial_state_;
  std::deque<Batch> batches_;  // deque so we can add/remote batches from either end
  std::deque<Block> blocks_;   // what actually gets drawn
  size_t total_events_;
  bool head_color_alternate_;
  bool backlog_growing_;
  bool backlog_growable_;  // false if we've found the room create message
  size_t min_backlog_size_;
  qreal content_height_;
  QString prev_batch_;  // Token for the batch immediately prior to the first message
  std::unordered_map<matrix::Content, Avatar> avatars_;
  QIcon avatar_missing_, avatar_loading_;

  void update_scrollbar();
  int visible_width() const;
  int block_spacing() const;
  int block_margin() const;
  int avatar_size() const;
  int block_body_start() const;
  int block_body_width() const;
  void grow_backlog();
  void prepend_batch(QString start, QString end, gsl::span<const matrix::proto::Event> events);
  int scrollback_trigger_size() const;
  void set_avatar(const matrix::Content &content, const QString &type, const QString &disposition, const QByteArray &data);
  void unref_avatar(const matrix::Content &);

  void prune_backlog();
  // Removes enough blocks from the backlog that calling for each new event will cause backlog size to approach one
  // batch size greater than min_backlog_size_. Requires but does not perform scrollbar update!
};

#endif
