#ifndef NATIVE_CHAT_TIMELINE_VIEW_HPP_
#define NATIVE_CHAT_TIMELINE_VIEW_HPP_

#include <deque>
#include <chrono>
#include <vector>
#include <memory>
#include <unordered_map>

#include <QAbstractScrollArea>
#include <QTextLayout>

#include "matrix/Event.hpp"
#include "matrix/Room.hpp"

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
  };

  class Block {
  public:
    Block(const TimelineView &, const matrix::RoomState &, const matrix::proto::Event &, std::shared_ptr<QPixmap>);
    void update_layout(const TimelineView &);
    void draw(const TimelineView &view, QPainter &p, QPointF offset) const;
    QRectF bounding_rect(const TimelineView &view) const;

    const QString &sender_id() const { return sender_id_; }

  private:
    const QString event_id_;
    const QString sender_id_;
    const std::shared_ptr<QPixmap> avatar_;

    QTextLayout name_layout_, timestamp_layout_;

    std::deque<Event> events_;  // deque so we can add events to either end
  };

  struct Batch {
    std::deque<Block> blocks;  // deque so we don't try to copy/move QTextLayout
    QString token;
  };

  matrix::Room &room_;
  matrix::RoomState initial_state_;
  std::deque<Batch> batches_;  // deque so we can add/remote batches from either end
  bool head_color_alternate_;
  bool backlog_growing_;
  bool backlog_growable_;  // false if we've found the room create message
  qreal content_height_;
  QString prev_batch_;  // Token for the batch immediately prior to the first message
  std::unordered_map<QString, std::shared_ptr<QPixmap>, QStringHash> avatars_;

  void update_scrollbar(bool for_prepend = false);
  int visible_width() const;
  int block_spacing() const;
  int block_margin() const;
  int avatar_size() const;
  void grow_backlog();
  void prepend_batch(QString start, QString end, gsl::span<const matrix::proto::Event> events);
  int scrollback_trigger_size() const;
};

#endif
