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
constexpr static std::chrono::minutes BLOCK_MERGE_INTERVAL(2);
constexpr static std::chrono::minutes TIMESTAMP_RANGE_THRESHOLD(2);

static auto to_time_point(uint64_t ts) {
  return std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::from_time_t(0))
      + std::chrono::duration<uint64_t, std::milli>(ts);
}

TimelineView::Event::Event(const TimelineView &view, const matrix::RoomState &state, const matrix::proto::Event &e)
    : system(e.type != "m.room.message"), time(to_time_point(e.origin_server_ts)) {
  QStringList lines;
  if(e.type == "m.room.message") {
    lines = e.content["body"].toString().split('\n');
  } else if(e.type == "m.room.member") {
    switch(matrix::parse_membership(e.content["membership"].toString()).value()) {
    case matrix::Membership::INVITE: {
      auto &invitee = *state.member(e.state_key);
      lines = QStringList(tr("invited %1").arg(state.member_name(invitee)));
      break;
    }
    case matrix::Membership::JOIN: {
      if(!e.unsigned_.prev_content) {
        lines = QStringList(tr("joined"));
        break;
      }
      const auto &prev = *e.unsigned_.prev_content;
      if(matrix::parse_membership(prev["membership"].toString()).value() == matrix::Membership::INVITE) {
        lines = QStringList(tr("accepted invite"));
        break;
      }
      const bool avatar_changed = QUrl(prev["avatar_url"].toString()) != QUrl(e.content["avatar_url"].toString());
      const auto new_dn = e.content["displayname"].toString();
      const bool dn_changed = prev["displayname"].toString() != new_dn;
      if(avatar_changed && dn_changed) {
        if(new_dn.isEmpty())
          lines = QStringList(tr("unset display name and changed avatar"));
        else
          lines = QStringList(tr("changed display name to %1 and changed avatar").arg(new_dn));
      } else if(avatar_changed) {
        lines = QStringList(tr("changed avatar"));
      } else if(dn_changed) {
        if(new_dn.isEmpty())
          lines = QStringList(tr("unset display name"));
        else
          lines = QStringList(tr("changed display name to %1").arg(new_dn));
      } else {
        lines = QStringList(tr("sent a no-op join"));
      }
      break;
    }
    case matrix::Membership::LEAVE: {
      lines = QStringList(tr("left"));
      break;
    }
    case matrix::Membership::BAN: {
      auto &banned = *state.member(e.state_key);
      lines = QStringList(tr("banned %1").arg(state.member_name(banned)));
      break;
    }
    }
  } else {
    lines = QStringList(tr("unrecognized event type %1").arg(e.type));
  }
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

  update_layout(view);
}

static QString to_timestamp(std::chrono::system_clock::time_point p) {
  auto time = std::chrono::system_clock::to_time_t(p);
  auto tm = std::localtime(&time);
  std::ostringstream s;
  s << std::put_time(tm, "%H:%M");
  return QString::fromStdString(s.str());
}

TimelineView::Block::Block(TimelineView &view, const matrix::RoomState &state, const matrix::proto::Event &e,
                           Event &e_internal)
    : sender_id_(e.sender) {
  events_.emplace_back(&e_internal);

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
  }

  update_layout(view);
}

void TimelineView::Block::update_layout(const TimelineView &view) {
  auto metrics = view.fontMetrics();

  {
    qreal height = 0;
    name_layout_.beginLayout();
    while(true) {
      auto line = name_layout_.createLine();
      if(!line.isValid()) break;
      line.setLineWidth(view.block_body_width());
      line.setPosition(QPointF(view.block_body_start(), height));
      height += metrics.lineSpacing();
    }
    name_layout_.endLayout();
  }

  {  // Lay out as a single or range timestamp as appropriate, degrading to single or nothing of space is unavailable
    auto layout_ts = [&](){
      timestamp_layout_.beginLayout();
      auto line = timestamp_layout_.createLine();
      line.setLineWidth(view.block_body_width());
      line.setPosition(QPointF(view.block_body_start(), 0));
      timestamp_layout_.endLayout();
      if(name_layout_.lineAt(0).naturalTextWidth() > view.block_body_width() - line.naturalTextWidth()) {
        timestamp_layout_.clearLayout();
        return false;
      }
      return true;
    };
    auto start_ts = to_timestamp(events_.front()->time);
    bool done = false;
    if(events_.size() > 1 && events_.back()->time - events_.front()->time > TIMESTAMP_RANGE_THRESHOLD) {
      auto end_ts = to_timestamp(events_.back()->time);
      timestamp_layout_.setText(start_ts % "–" % end_ts);
      done = layout_ts();
    }
    if(!done) {
      timestamp_layout_.setText(start_ts);
      layout_ts();
    }
  }
}

void TimelineView::Event::update_layout(const TimelineView &view) {
  auto metrics = view.fontMetrics();
  qreal height = 0;
  for(auto &layout : layouts) {
    layout.beginLayout();
    while(true) {
      auto line = layout.createLine();
      if(!line.isValid()) break;
      line.setLineWidth(view.block_body_width());
      line.setPosition(QPointF(view.block_body_start(), height));
      height += metrics.lineSpacing();
    }
    layout.endLayout();
  }
}

QRectF TimelineView::Event::bounding_rect() const {
  QRectF rect;
  for(const auto &layout : layouts) {
    rect |= layout.boundingRect();
  }
  return rect;
}

QRectF TimelineView::Block::bounding_rect(const TimelineView &view) const {
  auto metrics = view.fontMetrics();
  auto margin = view.block_margin();
  auto av_size = view.avatar_size();
  QRectF rect(margin, margin, view.visible_width() - margin, av_size);
  rect |= name_layout_.boundingRect();
  QPointF offset(0, name_layout_.boundingRect().height() + metrics.leading());
  for(const auto event : events_) {
    auto event_bounds = event->bounding_rect();
    rect |= event_bounds.translated(offset);
    offset.ry() += event_bounds.height() + metrics.leading();
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

  offset.ry() += name_layout_.boundingRect().height();

  for(const auto event : events_) {
    p.save();
    if(event->system) {
      p.setPen(header_color);
    }
    QRectF event_bounds;
    for(const auto &layout : event->layouts) {
      layout.draw(&p, offset);
      event_bounds |= layout.boundingRect();
    }
    offset.ry() += event_bounds.height() + metrics.leading();
    p.restore();
  }
}

size_t TimelineView::Block::size() const {
  return events_.size();
}

size_t TimelineView::Batch::size() const {
  return events.size();
}

TimelineView::TimelineView(matrix::Room &room, QWidget *parent)
    : QAbstractScrollArea(parent), room_(room), initial_state_(room.initial_state()), total_events_(0),
      head_color_alternate_(true), backlog_growing_(false), backlog_growable_(true), backlog_grow_cancelled_(false),
      min_backlog_size_(50), content_height_(0),
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

  assert(!batches_.empty());
  batches_.back().events.emplace_back(*this, state, in);
  auto &event = batches_.back().events.back();

  if(!blocks_.empty()
     && blocks_.back().sender_id() == in.sender
     && event.time - blocks_.back().events().back()->time <= BLOCK_MERGE_INTERVAL) {
    // Add to existing block
    blocks_.back().events().emplace_back(&event);
    blocks_.back().update_layout(*this);  // Updates timestamp if necessary
    content_height_ += blocks_.back().events().back()->bounding_rect().height() + fontMetrics().leading();
  } else {
    blocks_.emplace_back(*this, state, in, event);
    head_color_alternate_ = !head_color_alternate_;

    content_height_ += block_spacing();
    content_height_ += blocks_.back().bounding_rect(*this).height();
  }

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
    // Clobber empty batches
    if(batches_.back().events.empty()) return;
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
int TimelineView::block_body_start() const {
  return avatar_size() + 2*block_margin();
}
int TimelineView::block_body_width() const {
  return visible_width() - (block_body_start() + block_margin());
}

void TimelineView::update_scrollbar() {
  const auto view_height = viewport()->contentsRect().height();
  auto &scroll = *verticalScrollBar();
  const int old_maximum = scroll.maximum();
  const int fake_height = content_height_ + scrollback_trigger_size();
  const int old_value = scroll.value();
  scroll.setMaximum(view_height > fake_height ? 0 : fake_height - view_height);
  const int old_delta = (old_maximum - old_value);
  scroll.setValue(old_delta > scroll.maximum() ? 0 : scroll.maximum() - old_delta);
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
      for(auto &event : batch.events) {
        event.update_layout(*this);
      }
    }
    for(auto &block : blocks_) {
      block.update_layout(*this);
      content_height_ += block.bounding_rect(*this).height() + s;
    }
  }

  update_scrollbar();
  // Required uncondtionally since height *and* width matter due to text wrapping. Placed after content_height_ changes
  // to ensure they're accounted for.

  grow_backlog();  // In case we can newly see the end
}

void TimelineView::showEvent(QShowEvent *e) {
  (void)e;
  grow_backlog();  // Make sure we have something to display ASAP
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
  if(backlog_grow_cancelled_) {
    backlog_grow_cancelled_ = false;
    grow_backlog();
    return;
  }
  prev_batch_ = end;
  batches_.emplace_front();
  auto &batch = batches_.front();
  batch.token = start;
  for(const auto &e : events) {  // Events is in reverse order
    batch.events.emplace_front(*this, initial_state_, e);
    auto &internal = batch.events.front();
    if(!blocks_.empty()
       && blocks_.front().sender_id() == e.sender
       && internal.time - blocks_.front().events().front()->time <= BLOCK_MERGE_INTERVAL) {
      blocks_.front().events().emplace_front(&internal);
      blocks_.front().update_layout(*this);  // Updates timestamp if necessary
      content_height_ += blocks_.front().events().front()->bounding_rect().height() + fontMetrics().leading();
    } else {
      blocks_.emplace_front(*this, initial_state_, e, internal);
      content_height_ += blocks_.front().bounding_rect(*this).height() + block_spacing();
    }
    initial_state_.revert(e);
    backlog_growable_ &= e.type != "m.room.create";
  }

  total_events_ += events.size();

  update_scrollbar();

  grow_backlog();  // Check if the user is still seeing blank space.
}

void TimelineView::prune_backlog() {
  // Only prune if the user's looking the bottom.
  const auto &scroll = *verticalScrollBar();
  const int scroll_pos = scroll.value();
  if(scroll_pos != verticalScrollBar()->maximum()) return;
  const auto view_rect = viewport()->contentsRect();

  // We prune at least 2 events to ensure we don't hit a steady-state above the target
  size_t events_removed = 0;
  while(events_removed < 2) {
    auto &batch = batches_.front();  // Candidate for removal
    if(total_events_ - batch.size() < min_backlog_size_) return;

    // Determine the first batch that cannot be removed, i.e. the first to overlap the viewport
    // This is structurally very similar to the drawing loop.
    QPointF offset(0, view_rect.height() - (scroll_pos - scroll.maximum()));
    const Block *limit_block;
    for(auto it = blocks_.rbegin(); it != blocks_.rend(); ++it) {
      limit_block = &*it;
      const auto bounds = it->bounding_rect(*this);
      offset.ry() -= bounds.height();
      if((offset.y() + bounds.height()) < view_rect.top()) {
        // No further drawing possible
        break;
      }
      offset.ry() -= block_spacing();
    }

    // Find the block containing the last event to be removed
    Block *end_block = nullptr;
    for(auto it = blocks_.begin(); &*it != limit_block; ++it) {
      for(const auto event : it->events()) {
        if(event == &batch.events.back()) goto done;
      }
      continue;
      done:
      end_block = &*it;
      break;
    }
    if(!end_block) return;  // Batch overlaps with viewport, bail out

    int height_lost = 0;
    // Free blocks
    while(&blocks_.front() != end_block) {
      auto &block = blocks_.front();
      height_lost += block.bounding_rect(*this).height() + block_spacing();
      if(block.avatar()) unref_avatar(*block.avatar());
      blocks_.pop_front();
    }

    // Excise to-be-removed events from end_block, now the first block
    int original_first_block_height = blocks_.front().bounding_rect(*this).height() + block_spacing();
    while(blocks_.front().events().front() != &batch.events.back()) {
      blocks_.front().events().pop_front();
    }
    blocks_.front().events().pop_front();  // Remove final event

    // Free end_block if necessary, and compute final height change
    int final_first_block_height;
    if(blocks_.front().events().empty()) {
      if(blocks_.front().avatar()) unref_avatar(*blocks_.front().avatar());
      blocks_.pop_front();
      final_first_block_height = 0;
    } else {
      final_first_block_height = blocks_.front().bounding_rect(*this).height() + block_spacing();
    }
    height_lost += original_first_block_height - final_first_block_height;

    events_removed += batch.size();
    total_events_ -= batch.size();
    content_height_ -= height_lost;
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

void TimelineView::unref_avatar(const matrix::Content &content) {
  auto it = avatars_.find(content);
  --it->second.references;
  if(it->second.references == 0) {
    avatars_.erase(it);
  }
}

void TimelineView::reset() {
  batches_.clear();
  blocks_.clear();
  avatars_.clear();
  initial_state_ = room_.initial_state();
  total_events_ = 0;
  backlog_growable_ = true;
  content_height_ = 0;
  prev_batch_ = QString();  // Should be filled in again before control returns to Qt
  if(backlog_growing_) backlog_grow_cancelled_ = true;
}
