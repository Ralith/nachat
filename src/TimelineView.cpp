#include "TimelineView.hpp"

#include <sstream>
#include <iomanip>
#include <ctime>

#include <QDebug>
#include <QScrollBar>
#include <QPainter>

const static QColor text_color = Qt::black;
const static QColor primary_bg(245, 245, 245);
const static QColor secondary_bg = Qt::white;
const static QColor header_color(96, 96, 96);

static auto to_time_point(uint64_t ts) {
  return std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::from_time_t(0))
      + std::chrono::duration<uint64_t, std::milli>(ts);
}

TimelineView::Event::Event(const TimelineView &view, const matrix::proto::Event &e)
    : time(to_time_point(e.origin_server_ts)) {
  auto lines = e.content["body"].toString().split('\n');
  layouts = std::vector<QTextLayout>(lines.size());
  QTextOption body_options;
  body_options.setAlignment(Qt::AlignLeft | Qt::AlignTop);
  body_options.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  for(int i = 0; i < lines.size(); ++i) {
    layouts[i].setFont(view.font());
    layouts[i].setTextOption(body_options);
    layouts[i].setCacheEnabled(true);
    layouts[i].setText(lines[i]);
  }
}

TimelineView::Block::Block(const TimelineView &view, const matrix::RoomState &state, const matrix::proto::Event &e,
                           std::shared_ptr<QPixmap> avatar)
    : sender_id_(e.sender), avatar_(std::move(avatar)) {
  events_.emplace_back(view, e);

  {
    QTextOption options;
    options.setAlignment(Qt::AlignLeft | Qt::AlignTop);
    options.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    name_layout_.setFont(view.font());
    name_layout_.setTextOption(options);
    name_layout_.setCacheEnabled(true);
    if(auto sender = state.member(e.sender)) {
      name_layout_.setText(state.member_name(*sender));
    }
  }

  {
    QTextOption options;
    options.setAlignment(Qt::AlignRight | Qt::AlignTop);
    options.setWrapMode(QTextOption::NoWrap);
    timestamp_layout_.setFont(view.font());
    timestamp_layout_.setTextOption(options);
    timestamp_layout_.setCacheEnabled(true);

    auto time = std::chrono::system_clock::to_time_t(events_.front().time);
    auto tm = std::localtime(&time);
    std::ostringstream s;
    s << std::put_time(tm, "%H:%M");

    timestamp_layout_.setText(QString::fromStdString(s.str()));
  }

  update_layout(view);
}

void TimelineView::Block::update_layout(const TimelineView &view) {
  auto metrics = view.fontMetrics();
  const int margin = view.block_margin();
  const int avatar_size = view.avatar_size();
  qreal height = 0;

  const int line_start = avatar_size + 2*margin;
  const int line_width = view.visible_width() - (line_start + margin);

  {
    name_layout_.beginLayout();
    while(true) {
      auto line = name_layout_.createLine();
      if(!line.isValid()) break;
      line.setLineWidth(line_width);
      line.setPosition(QPointF(line_start, height));
      height += metrics.lineSpacing();
    }
    name_layout_.endLayout();
  }

  {
    timestamp_layout_.beginLayout();
    auto line = timestamp_layout_.createLine();
    line.setLineWidth(line_width);
    line.setPosition(QPointF(line_start, 0));
    timestamp_layout_.endLayout();
    if(metrics.boundingRect(name_layout_.text()).width() > line_width - metrics.boundingRect(timestamp_layout_.text()).width()) {
      timestamp_layout_.clearLayout();
    }
  }

  for(auto &event : events_) {
    for(auto &layout : event.layouts) {
      layout.beginLayout();
      while(true) {
        auto line = layout.createLine();
        if(!line.isValid()) break;
        line.setLineWidth(line_width);
        line.setPosition(QPointF(line_start, height));
        height += metrics.lineSpacing();
      }
      layout.endLayout();
    }
  }
}

QRectF TimelineView::Block::bounding_rect(const TimelineView &view) const {
  auto margin = view.block_margin();
  auto av_size = view.avatar_size();
  QRectF rect(margin, margin, view.visible_width() - margin, av_size);
  rect |= name_layout_.boundingRect();
  for(const auto &event : events_) {
    for(const auto &layout : event.layouts) {
      rect |= layout.boundingRect();
    }
  }
  return rect;
}

void TimelineView::Block::draw(const TimelineView &view, QPainter &p, QPointF offset) const {
  auto metrics = view.fontMetrics();
  auto margin = view.block_margin();

  if(avatar_)
    p.drawPixmap(QPoint(offset.x() + margin, offset.y()), *avatar_);

  p.save();
  p.setPen(header_color);
  name_layout_.draw(&p, offset);
  timestamp_layout_.draw(&p, offset);
  p.restore();

  for(const auto &event : events_) {
    for(const auto &layout : event.layouts) {
      layout.draw(&p, offset);
    }
  }
}

TimelineView::TimelineView(QWidget *parent)
    : QAbstractScrollArea(parent), content_height_(0) {
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  verticalScrollBar()->setSingleStep(20);  // Taken from QScrollArea
}

int TimelineView::visible_width() const {
  return viewport()->contentsRect().width();
}

void TimelineView::push_back(const matrix::RoomState &state, const matrix::proto::Event &in) {
  std::shared_ptr<QPixmap> avatar;
  auto it = avatars_.find(in.sender);
  if(it == avatars_.end()) {
    auto size = avatar_size();
    avatar = avatars_.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(in.sender),
      std::forward_as_tuple(new QPixmap(size, size))).first->second;
    avatar->fill(Qt::black);
  } else {
    avatar = it->second;
  }
  blocks_.emplace_back(*this, state, in, std::move(avatar));

  content_height_ += block_spacing();
  content_height_ += blocks_.back().bounding_rect(*this).height();

  update_scrollbar();

  viewport()->update();
}

int TimelineView::block_margin() const { return fontMetrics().lineSpacing()/3; }
int TimelineView::block_spacing() const { return fontMetrics().lineSpacing()/3; }
int TimelineView::avatar_size() const {
  auto metrics = fontMetrics();
  return metrics.height() * 2 + metrics.leading();
}

void TimelineView::update_scrollbar() {
  auto &scroll = *verticalScrollBar();
  const bool initially_at_bottom = scroll.value() == scroll.maximum();
  auto view_height = viewport()->contentsRect().height();
  scroll.setMaximum(view_height > content_height_ ? 0 : content_height_ - view_height);
  if(initially_at_bottom) {
    scroll.setValue(scroll.maximum());
  }
}

void TimelineView::paintEvent(QPaintEvent *) {
  QRectF view_rect = viewport()->contentsRect();
  view_rect.setWidth(view_rect.width());
  QPainter painter(viewport());
  painter.fillRect(view_rect, Qt::lightGray);
  painter.setPen(Qt::black);
  auto &scroll = *verticalScrollBar();
  QPointF offset(0, view_rect.height() - (scroll.value() - scroll.maximum()));
  auto s = block_spacing();
  bool alternate = blocks_.size() % 2;
  const int margin = block_margin();
  for(auto it = blocks_.crbegin(); it != blocks_.crend(); ++it) {
    const auto bounds = it->bounding_rect(*this);
    offset.ry() -= bounds.height();
    if((offset.y() + bounds.height()) < view_rect.top()) {
      // No further drawing possible
      break;
    }
    if(offset.y() < view_rect.bottom()) {
      {
        QRectF outline(offset.x() + 0.5, offset.y() - (0.5 + s/2.0),
                       view_rect.width() - 1,
                       bounds.translated(offset).height() + (1 + s/2.0));
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(outline, margin*2, margin*2);
        painter.fillPath(path, alternate ? secondary_bg : primary_bg);
        painter.restore();
      }
      it->draw(*this, painter, offset);
    }
    offset.ry() -= s;
    alternate = !alternate;
  }
}

void TimelineView::resizeEvent(QResizeEvent *e) {
  update_scrollbar();

  {
    const int unit = avatar_size() + block_spacing();
    const int window_height = viewport()->contentsRect().height();
    const int window_units = window_height / unit;

    verticalScrollBar()->setPageStep((window_units - 1) * unit);
  }

  if(e->size().width() != e->oldSize().width()) {
    // Linebreaks may have changed, so we need to lay everything out again
    content_height_ = 0;
    const auto s = block_spacing();
    for(auto &block : blocks_) {
      block.update_layout(*this);
      content_height_ += block.bounding_rect(*this).height() + s;
    }
    update_scrollbar();
  }
}
