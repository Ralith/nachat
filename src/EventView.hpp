#ifndef NATIVE_CHAT_EVENT_VIEW_HPP_
#define NATIVE_CHAT_EVENT_VIEW_HPP_

#include <chrono>
#include <deque>
#include <vector>
#include <experimental/optional>

#include <QFont>
#include <QPalette>
#include <QFontMetrics>
#include <QTextLayout>

#include "matrix/Event.hpp"
#include "matrix/Content.hpp"
#include "matrix/Room.hpp"

class QShortcut;
class QMenu;

class Event;

class BlockRenderInfo {
public:
  BlockRenderInfo(const matrix::UserID &self, const QPalette &p, const QFont &f, qreal w) : self_(self), palette_(p), font_(f), viewport_width_(w) {}

  const matrix::UserID &self() const { return self_; }
  qreal width() const { return viewport_width_; }
  qreal body_width() const { return width() - (body_start() + margin()); }
  qreal body_start() const { return avatar_size() + 2*margin(); }
  qreal avatar_size() const { return metrics().height() * 2 + metrics().leading(); }
  qreal margin() const { return metrics().lineSpacing() * 0.33; }
  qreal spacing() const { return metrics().lineSpacing() * 0.75; }
  qreal event_spacing() const { return metrics().leading(); }
  QFontMetrics metrics() const { return QFontMetrics(font_); }
  const QFont &font() const { return font_; }
  const QPalette &palette() const { return palette_; }

  std::vector<std::pair<QString, QVector<QTextLayout::FormatRange>>>
    format_text(const matrix::RoomState &state, const matrix::event::Room &evt, const QString &str) const;

private:
  matrix::UserID self_;
  QPalette palette_;
  QFont font_;
  qreal viewport_width_;
};

class Event {
public:
  matrix::event::Room data;
  std::vector<QTextLayout> layouts;
  const std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds> time;

  Event(const BlockRenderInfo &, const matrix::RoomState &, const matrix::event::Room &);
  QRectF bounding_rect() const;
  void update_layout(const BlockRenderInfo &);

  void event(matrix::Room &room, QWidget &container, const std::experimental::optional<QPointF> &pos, QEvent *e);

  void populate_menu(matrix::Room &room, QMenu &menu, const QPointF &pos) const;

  std::experimental::optional<QUrl> link_at(const QPointF &pos) const;
  std::experimental::optional<QUrl> link_at(const QTextLayout &layout, int cursor) const;
};

struct EventHit {
  QPointF pos;
  Event *event;
  explicit operator bool(){ return event; }
};

class Block {
public:
  Block(const BlockRenderInfo &, const matrix::RoomState &, Event &);
  void update_header(const BlockRenderInfo &, const matrix::RoomState &state);
  void update_layout(const BlockRenderInfo &);
  void draw(const BlockRenderInfo &, QPainter &p, QPointF offset,
            const QPixmap &avatar,
            bool focused,
            bool select_all,
            std::experimental::optional<QPointF> select_start,
            std::experimental::optional<QPointF> select_end) const;
  QRectF bounding_rect(const BlockRenderInfo &) const;
  QString selection_text(const QFontMetrics &, bool select_all,
                         std::experimental::optional<QPointF> select_start,
                         std::experimental::optional<QPointF> select_end) const;

  const matrix::UserID &sender_id() const { return sender_id_; }
  size_t size() const;
  const std::experimental::optional<matrix::Content> &avatar() const { return avatar_; }
  EventHit event_at(const QFontMetrics &, const QPointF &);

  std::deque<Event *> &events() { return events_; }
  const std::deque<Event *> &events() const { return events_; }

  void event(matrix::Room &room, QWidget &container, const BlockRenderInfo &, const std::experimental::optional<QPointF> &pos, QEvent *event);
  qreal header_height() const { return name_layout_.boundingRect().height(); }

private:
  const matrix::UserID sender_id_;
  std::experimental::optional<matrix::Content> avatar_;

  QTextLayout name_layout_, timestamp_layout_;

  std::deque<Event *> events_;  // deque so we can add events to either end
};

#endif
