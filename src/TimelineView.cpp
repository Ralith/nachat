#include "TimelineView.hpp"

#include <sstream>
#include <iomanip>
#include <ctime>

#include <QDebug>
#include <QScrollBar>
#include <QPainter>

#include "matrix/Session.hpp"

const static QColor text_color = Qt::black;
const static QColor primary_bg(245, 245, 245);
const static QColor secondary_bg = Qt::white;
const static QColor header_color(96, 96, 96);
constexpr static int BACKLOG_BATCH_SIZE = 50;

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

TimelineView::Block::Block(TimelineView &view, const matrix::RoomState &state, const matrix::proto::Event &e)
    : sender_id_(e.sender) {
  events_.emplace_back(view, e);

  auto sender = state.member(e.sender);

  if(sender && !sender->avatar_url().isEmpty()) {
    try {
      avatar_ = matrix::Content(sender->avatar_url());
    } catch(const std::invalid_argument &e) {
      qDebug() << "invalid avatar URL:" << e.what() << ":" << sender->avatar_url();
    }

    if(avatar_) {
      auto result = view.avatars_.emplace(std::piecewise_construct,
                                          std::forward_as_tuple(*avatar_),
                                          std::forward_as_tuple());
      if(result.second) {
        auto reply = view.room_.session().get_thumbnail(result.first->first, QSize(view.avatar_size(), view.avatar_size()));
        connect(reply, &matrix::ContentFetch::finished, &view, &TimelineView::set_avatar);
        connect(reply, &matrix::ContentFetch::error, &view.room_, &matrix::Room::error);
      }
      ++result.first->second.references;
    }
  }

  {
    QTextOption options;
    options.setAlignment(Qt::AlignLeft | Qt::AlignTop);
    options.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    name_layout_.setFont(view.font());
    name_layout_.setTextOption(options);
    name_layout_.setCacheEnabled(true);
    if(sender) {
      name_layout_.setText(state.member_name(*sender));
    } else {
      name_layout_.setText(sender_id_);
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

  QPixmap avatar_pixmap;
  const auto size = view.avatar_size();
  if(avatar_) {
    auto a = view.avatars_.at(*avatar_);
    if(a.pixmap.isNull()) {
      avatar_pixmap = view.avatar_loading_.pixmap(size, size);
    } else {
      avatar_pixmap = a.pixmap;
    }
  } else {
    avatar_pixmap = view.avatar_missing_.pixmap(size, size);
  }
  p.drawPixmap(QPoint(offset.x() + margin + (size - avatar_pixmap.width()) / 2,
                      offset.y() + (size - avatar_pixmap.height()) / 2),
               avatar_pixmap);

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

size_t TimelineView::Block::size() const {
  return events_.size();
}

size_t TimelineView::Batch::size() const {
  size_t result = 0;
  for(const auto &block : blocks) {
    result += block.size();
  }
  return result;
}

TimelineView::TimelineView(matrix::Room &room, QWidget *parent)
    : QAbstractScrollArea(parent), room_(room), initial_state_(room.initial_state()), total_events_(0),
      head_color_alternate_(true), backlog_growing_(false), backlog_growable_(true), min_backlog_size_(256),
      content_height_(0),
      avatar_missing_(QIcon::fromTheme("unknown")),
      avatar_loading_(QIcon::fromTheme("image-loading")) {
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  verticalScrollBar()->setSingleStep(20);  // Taken from QScrollArea
  setFocusPolicy(Qt::NoFocus);

  connect(verticalScrollBar(), &QAbstractSlider::valueChanged, [this](int value) {
      (void)value;
      grow_backlog();
    });
}

int TimelineView::visible_width() const {
  return viewport()->contentsRect().width();
}

void TimelineView::push_back(const matrix::RoomState &state, const matrix::proto::Event &in) {
  backlog_growable_ &= in.type != "m.room.create";

  batches_.back().blocks.emplace_back(*this, state, in);
  head_color_alternate_ = !head_color_alternate_;

  content_height_ += block_spacing();
  content_height_ += batches_.back().blocks.back().bounding_rect(*this).height();
  total_events_ += 1;

  prune_backlog();

  update_scrollbar();

  viewport()->update();
}

void TimelineView::end_batch(const QString &token) {
  if(batches_.empty()) {
    prev_batch_ = token;
  } else {
    batches_.back().token = token;
  }
  batches_.emplace_back();  // Next batch
}

int TimelineView::block_margin() const { return fontMetrics().lineSpacing()/3; }
int TimelineView::block_spacing() const { return fontMetrics().lineSpacing()/3; }
int TimelineView::avatar_size() const {
  auto metrics = fontMetrics();
  return metrics.height() * 2 + metrics.leading();
}
int TimelineView::scrollback_trigger_size() const {
  return viewport()->contentsRect().height()/2;
}

void TimelineView::update_scrollbar(bool for_prepend) {
  auto &scroll = *verticalScrollBar();
  const bool initially_at_bottom = scroll.value() == scroll.maximum();
  auto view_height = viewport()->contentsRect().height();
  const int old_maximum = scroll.maximum();
  const int fake_height = content_height_ + scrollback_trigger_size();
  scroll.setMaximum(view_height > fake_height ? 0 : fake_height - view_height);
  if(initially_at_bottom) {
    scroll.setValue(scroll.maximum());
  } else if(for_prepend) {
    scroll.setValue(scroll.value() + (scroll.maximum() - old_maximum));
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
  bool alternate = head_color_alternate_;
  const int margin = block_margin();
  for(auto batch_it = batches_.crbegin(); batch_it != batches_.crend(); ++batch_it) {
    for(auto it = batch_it->blocks.crbegin(); it != batch_it->blocks.crend(); ++it) {
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
}

void TimelineView::resizeEvent(QResizeEvent *e) {
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
    for(auto &batch : batches_) {
      for(auto &block : batch.blocks) {
        block.update_layout(*this);
        content_height_ += block.bounding_rect(*this).height() + s;
      }
    }
  }

  update_scrollbar();
  // Required uncondtionally since height *and* width matter due to text wrapping. Placed after content_height_ changes
  // to ensure they're accounted for.
}

void TimelineView::grow_backlog() {
  if(verticalScrollBar()->value() >= scrollback_trigger_size() || backlog_growing_ || !backlog_growable_) return;
  backlog_growing_ = true;
  auto reply = room_.get_messages(matrix::Room::Direction::BACKWARD, prev_batch_, BACKLOG_BATCH_SIZE);
  connect(reply, &matrix::MessageFetch::finished, this, &TimelineView::prepend_batch);
  connect(reply, &matrix::MessageFetch::error, &room_, &matrix::Room::error);
}

void TimelineView::prepend_batch(QString start, QString end, gsl::span<const matrix::proto::Event> events) {
  backlog_growing_ = false;
  prev_batch_ = end;
  batches_.emplace_front();
  auto &batch = batches_.front();
  batch.token = start;  // FIXME: Verify that this is correct
  for(const auto &e : events) {  // Events is in reverse order
    batch.blocks.emplace_front(*this, initial_state_, e);
    content_height_ += batch.blocks.front().bounding_rect(*this).height() + block_spacing();
    initial_state_.revert(e);
    backlog_growable_ &= e.type != "m.room.create";
  }

  total_events_ += events.size();

  update_scrollbar(true);

  grow_backlog();  // Check if the user is still seeing blank space.
}

void TimelineView::prune_backlog() {
  // Only prune if the user's looking the bottom.
  const int scroll_pos = verticalScrollBar()->value();
  if(scroll_pos != verticalScrollBar()->maximum()) return;

  // We prune at least 2 events to ensure we don't hit a steady-state above the target
  size_t events_removed = 0;
  while(events_removed < 2) {
    const size_t front_size = batches_.front().size();
    int batch_height = 0;
    for(const auto &block : batches_.front().blocks) {
      batch_height += block.bounding_rect(*this).height() + block_spacing();
    }
    if((total_events_ - front_size < min_backlog_size_)
       || (batch_height + scrollback_trigger_size() > scroll_pos)) {
      // If removing the earliest batch would cause us to fall under the minimum, or the user could see the removed
      // batch, bail out.
      return;
    }

    // GC avatars
    for(auto &block : batches_.front().blocks) {
      if(!block.avatar()) continue;
      auto it = avatars_.find(*block.avatar());
      --it->second.references;
      if(it->second.references == 0) {
        avatars_.erase(it);
      }
    }

    total_events_ -= front_size;
    content_height_ -= batch_height;
    events_removed += front_size;
    batches_.pop_front();
    backlog_growable_ = true;
  }
}

void TimelineView::set_avatar(const matrix::Content &content, const QString &type, const QString &disposition,
                              const QByteArray &data) {
  (void)disposition;
  QPixmap pixmap;
  pixmap.loadFromData(data);
  const auto size = avatar_size();
  if(pixmap.width() > size || pixmap.height() > size)
    pixmap = pixmap.scaled(avatar_size(), avatar_size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
  avatars_.at(content).pixmap = pixmap;
  viewport()->update();
}
