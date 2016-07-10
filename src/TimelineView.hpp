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
  TimelineView(QWidget *parent = nullptr);

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

    std::deque<Event> events_;
  };

  std::deque<Block> blocks_;
  qreal content_height_;
  std::unordered_map<QString, std::shared_ptr<QPixmap>, QStringHash> avatars_;

  void update_scrollbar();
  int visible_width() const;
  int block_spacing() const;
  int block_margin() const;
  int avatar_size() const;
};

#endif
