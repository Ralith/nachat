#include "TimelineView.hpp"

#include <chrono>

#include <QDebug>
#include <QScrollBar>
#include <QPainter>
#include <QShortcut>
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QRegularExpression>
#include <QTimer>
#include <QToolTip>

#include "matrix/Session.hpp"
#include "Spinner.hpp"

using std::experimental::optional;

constexpr static int BACKLOG_BATCH_SIZE = 50;
constexpr static std::chrono::minutes BLOCK_MERGE_INTERVAL(2);

size_t TimelineView::Batch::size() const {
  return events.size();
}

TimelineView::TimelineView(matrix::Room &room, QWidget *parent)
    : QAbstractScrollArea(parent), room_(room), initial_state_(room.initial_state()), total_events_(0),
      head_color_alternate_(true), backlog_growing_(false), backlog_growable_(true), backlog_grow_cancelled_(false),
      min_backlog_size_(50), content_height_(0),
      avatar_unset_(QIcon::fromTheme("unknown")),
      avatar_loading_(QIcon::fromTheme("image-loading", avatar_unset_)),
      copy_(new QShortcut(QKeySequence::Copy, this)),
      grabbed_focus_(nullptr) {
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  verticalScrollBar()->setSingleStep(20);  // Taken from QScrollArea
  setMouseTracking(true);
  setMinimumSize(minimumSizeHint());

  QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  policy.setHorizontalStretch(1);
  policy.setVerticalStretch(0);
  setSizePolicy(policy);

  connect(verticalScrollBar(), &QAbstractSlider::valueChanged, [this](int value) {
      (void)value;
      grow_backlog();
    });
  connect(copy_, &QShortcut::activated, this, &TimelineView::copy);

  {
    const int extent = devicePixelRatioF() * (scrollback_status_size() - block_info().spacing());
    spinner_ = QPixmap(extent, extent);
    spinner_.fill(Qt::transparent);
    QPainter painter(&spinner_);
    painter.setRenderHint(QPainter::Antialiasing);
    Spinner::paint(palette().color(QPalette::Shadow), palette().color(QPalette::Base), painter, extent);
    spinner_.setDevicePixelRatio(devicePixelRatioF());
  }
}

void TimelineView::push_back(const matrix::RoomState &state, const matrix::proto::Event &in) {
  backlog_growable_ &= in.type != "m.room.create";

  assert(!batches_.empty());
  batches_.back().events.emplace_back(block_info(), state, in);
  auto &event = batches_.back().events.back();

  if(!blocks_.empty()
     && blocks_.back().sender_id() == in.sender
     && event.time - blocks_.back().events().back()->time <= BLOCK_MERGE_INTERVAL) {
    // Add to existing block
    auto &block = blocks_.back();
    content_height_ -= block.bounding_rect(block_info()).height();
    block.events().emplace_back(&event);
    block.update_header(block_info(), state); // Updates timestamp if necessary
    content_height_ += block.bounding_rect(block_info()).height();
  } else {
    blocks_.emplace_back(block_info(), state, in, event);
    if(blocks_.back().avatar()) ref_avatar(*blocks_.back().avatar());
    head_color_alternate_ = !head_color_alternate_;

    content_height_ += block_info().spacing();
    content_height_ += blocks_.back().bounding_rect(block_info()).height();
  }

  total_events_ += 1;

  prune_backlog();

  update_scrollbar(false);

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

int TimelineView::scrollback_trigger_size() const {
  return viewport()->contentsRect().height()*2;
}
int TimelineView::scrollback_status_size() const {
  auto metrics = fontMetrics();
  return metrics.height() * 4;
}
BlockRenderInfo TimelineView::block_info() const {
  return BlockRenderInfo(room_.session().user_id(), palette(), font(), viewport()->contentsRect().width());
}

void TimelineView::update_scrollbar(bool grew_upward) {
  const auto view_height = viewport()->contentsRect().height();
  auto &scroll = *verticalScrollBar();
  const int fake_height = content_height_ + scrollback_status_size();
  const bool was_at_bottom = scroll.value() == scroll.maximum();
  const int old_wrt_bottom = scroll.maximum() - scroll.value();
  scroll.setMaximum(view_height > fake_height ? 0 : fake_height - view_height);
  if(was_at_bottom) {
    // Always remain at the bottom if we started there
    scroll.setValue(scroll.maximum());
  } else if(grew_upward) {
    // Stay fixed relative to bottom if the top grew
    scroll.setValue(old_wrt_bottom > scroll.maximum() ? 0 : scroll.maximum() - old_wrt_bottom);
  }
  // Otherwise we want to stay fixed relative to the top, i.e. do nothing
}

void TimelineView::paintEvent(QPaintEvent *) {
  const QRectF view_rect = viewport()->contentsRect();
  QPainter painter(viewport());
  painter.fillRect(view_rect, palette().color(QPalette::Dark));
  painter.setPen(palette().color(QPalette::Text));
  auto &scroll = *verticalScrollBar();
  QPointF offset(0, view_rect.height() - (scroll.value() - scroll.maximum()));
  const float half_spacing = block_info().spacing()/2.0;
  bool alternate = head_color_alternate_;
  const int margin = block_info().margin();
  visible_blocks_.clear();
  SelectionScanner ss(selection_);
  for(auto it = blocks_.rbegin(); it != blocks_.rend(); ++it) {
    ss.advance(&*it);
    const auto bounds = it->bounding_rect(block_info());
    offset.ry() -= bounds.height() + half_spacing;
    if((offset.y() + bounds.height() + half_spacing) < view_rect.top()) {
      // No further drawing possible
      break;
    }
    if(offset.y() - half_spacing < view_rect.bottom()) {
      visible_blocks_.push_back(VisibleBlock{&*it, bounds.translated(offset)});
      {
        const QRectF outline(offset.x(), offset.y() - half_spacing,
                             view_rect.width(), bounds.height() + block_info().spacing() + 0.5 / devicePixelRatioF());
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(outline, margin*2, margin*2);
        painter.fillPath(path, palette().color(alternate ? QPalette::AlternateBase : QPalette::Base));
        painter.restore();
      }
      QPixmap avatar_pixmap;
      const auto av_size = block_info().avatar_size();
      if(it->avatar()) {
        auto a = avatars_.at(*it->avatar());
        if(a.pixmap.isNull()) {
          avatar_pixmap = avatar_loading_.pixmap(av_size, av_size);
        } else {
          avatar_pixmap = a.pixmap;
        }
      } else {
        avatar_pixmap = avatar_unset_.pixmap(av_size, av_size);
      }
      it->draw(block_info(), painter, offset, avatar_pixmap, hasFocus(), ss.fully_selected(&*it), ss.start_point(), ss.end_point());
    }
    offset.ry() -= half_spacing;
    alternate = !alternate;
  }
  if(backlog_growing_ && offset.y() > view_rect.top()) {
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const qreal extent = spinner_.width() / spinner_.devicePixelRatio();
    painter.translate(view_rect.width() / 2., offset.y() - view_rect.top() - (extent/2. + half_spacing));
    auto t = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now());
    const qreal rotation_seconds = 2;
    const qreal angle = 360. * static_cast<qreal>(t.time_since_epoch().count() % static_cast<uint64_t>(1000 * rotation_seconds)) / (1000 * rotation_seconds);
    painter.rotate(angle);
    painter.drawPixmap(QPointF(-extent/2., -extent/2.), spinner_);
    painter.restore();
    QTimer::singleShot(30, viewport(), static_cast<void (QWidget::*)()>(&QWidget::update));
  }
}

void TimelineView::resizeEvent(QResizeEvent *e) {
  verticalScrollBar()->setPageStep(viewport()->contentsRect().height() * 0.75);

  if(e->size().width() != e->oldSize().width()) {
    // Linebreaks may have changed, so we need to lay everything out again
    content_height_ = 0;
    const auto s = block_info().spacing();
    for(auto &batch : batches_) {
      for(auto &event : batch.events) {
        event.update_layout(block_info());
      }
    }
    for(auto &block : blocks_) {
      block.update_layout(block_info());
      content_height_ += block.bounding_rect(block_info()).height() + s;
    }
  }

  update_scrollbar(false);
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
  connect(reply, &matrix::MessageFetch::error, this, &TimelineView::backlog_grow_error);
}

void TimelineView::backlog_grow_error() {
  backlog_growing_ = false;
  if(backlog_grow_cancelled_) {
    backlog_grow_cancelled_ = false;
  }
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
    initial_state_.ensure_member(e); // Make sure a just-departed member is accounted for
    batch.events.emplace_front(block_info(), initial_state_, e);
    auto &internal = batch.events.front();
    if(!blocks_.empty()
       && blocks_.front().sender_id() == e.sender
       && blocks_.front().events().front()->time - internal.time <= BLOCK_MERGE_INTERVAL) {
      content_height_ -= blocks_.front().bounding_rect(block_info()).height();
      blocks_.front().events().emplace_front(&internal);
      optional<matrix::Content> original_avatar = blocks_.front().avatar();
      blocks_.front().update_header(block_info(), initial_state_);  // Updates timestamp, disambig. display name, and avatar if necessary
      const optional<matrix::Content> &new_avatar = blocks_.front().avatar();
      if(new_avatar != original_avatar) {
        if(original_avatar) {
          unref_avatar(*original_avatar);
        }
        if(new_avatar) {
          ref_avatar(*new_avatar);
        }
      }
      content_height_ += blocks_.front().bounding_rect(block_info()).height();
    } else {
      blocks_.emplace_front(block_info(), initial_state_, e, internal);
      const auto &av = blocks_.front().avatar();
      if(av) ref_avatar(*av);
      content_height_ += blocks_.front().bounding_rect(block_info()).height() + block_info().spacing();
    }
    initial_state_.revert(e);
    backlog_growable_ &= e.type != "m.room.create";
  }

  total_events_ += events.size();

  update_scrollbar(true);

  grow_backlog();  // Check if the user is still seeing blank space.
  viewport()->update();
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

    // Determine the first batch that cannot be removed, i.e. the first to overlap the scrollback trigger area
    QPointF offset(0, view_rect.height() - (scroll_pos - scroll.maximum()));
    const Block *limit_block;
    for(auto it = blocks_.rbegin(); it != blocks_.rend(); ++it) {
      limit_block = &*it;
      const auto bounds = it->bounding_rect(block_info());
      offset.ry() -= bounds.height();
      if((offset.y() + bounds.height()) < view_rect.top() - scrollback_trigger_size()) {
        break;
      }
      offset.ry() -= block_info().spacing();
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
    if(!end_block) break;  // Batch overlaps with viewport, bail out

    int height_lost = 0;
    // Free blocks
    while(&blocks_.front() != end_block) {
      auto &block = blocks_.front();
      height_lost += block.bounding_rect(block_info()).height() + block_info().spacing();
      if(block.avatar()) unref_avatar(*block.avatar());
      pop_front_block();
    }

    // Excise to-be-removed events from end_block, now the first block
    int original_first_block_height = blocks_.front().bounding_rect(block_info()).height() + block_info().spacing();
    while(blocks_.front().events().front() != &batch.events.back()) {
      blocks_.front().events().pop_front();
    }
    blocks_.front().events().pop_front();  // Remove final event

    // Free end_block if necessary, and compute final height change
    int final_first_block_height;
    if(blocks_.front().events().empty()) {
      if(blocks_.front().avatar()) unref_avatar(*blocks_.front().avatar());
      pop_front_block();
      final_first_block_height = 0;
    } else {
      final_first_block_height = blocks_.front().bounding_rect(block_info()).height() + block_info().spacing();
    }
    height_lost += original_first_block_height - final_first_block_height;

    events_removed += batch.size();
    total_events_ -= batch.size();
    content_height_ -= height_lost;
    batches_.pop_front();
    backlog_growable_ = true;
  }
  if(events_removed > 0)
    qDebug() << room_.id() << "pruned" << events_removed << "events";
}

void TimelineView::pop_front_block() {
  if(selection_) {
    if(blocks_.size() == 1) {
      selection_ = {};
    } else {
      if(selection_->end == &blocks_[0])
        selection_->end = &blocks_[1];
      if(selection_->start == &blocks_[0])
        selection_->start = &blocks_[1];
    }
  }
  blocks_.pop_front();
}

void TimelineView::set_avatar(const matrix::Content &content, const QString &type, const QString &disposition,
                              const QByteArray &data) {
  (void)disposition;
  auto it = avatars_.find(content);
  if(it == avatars_.end()) return;  // Avatar is no longer necessary

  QPixmap pixmap;
  pixmap.loadFromData(data, QMimeDatabase().mimeTypeForName(type.toUtf8()).preferredSuffix().toUtf8().constData());
  if(pixmap.isNull()) pixmap.loadFromData(data);

  const auto size = block_info().avatar_size() * devicePixelRatioF();
  if(pixmap.width() > size || pixmap.height() > size)
    pixmap = pixmap.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  pixmap.setDevicePixelRatio(devicePixelRatioF());
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

void TimelineView::mousePressEvent(QMouseEvent *event) {
  auto b = dispatch_event(event->localPos(), event);
  if(event->button() == Qt::LeftButton) {

    QApplication::setOverrideCursor(Qt::IBeamCursor);
    if(b) {
      grabbed_focus_ = b->block;
      const auto rel_pos = event->localPos() - b->bounds.topLeft();
      selection_ = Selection{b->block, rel_pos, b->block, rel_pos};
    } else {
      selection_ = {};
    }
    viewport()->update();
  }
}

void TimelineView::mouseMoveEvent(QMouseEvent *event) {
  viewport()->setCursor(Qt::ArrowCursor);
  auto b = dispatch_event(event->localPos(), event);
  if(b && event->buttons() & Qt::LeftButton) {
    // Update selection
    // TODO: Start auto-scrolling if cursor out of viewport
    const auto rel_pos = event->localPos() - b->bounds.topLeft();
    if(!selection_) {
      selection_ = Selection{};
      selection_->end = b->block;
      selection_->end_pos = rel_pos;
    }
    selection_->end = b->block;
    selection_->end_pos = rel_pos;
    QString t = selection_text();
    if(!t.isEmpty())
      QApplication::clipboard()->setText(selection_text(), QClipboard::Selection);

    viewport()->update();
  }
}

void TimelineView::mouseReleaseEvent(QMouseEvent *event) {
  dispatch_event(event->localPos(), event);
  grabbed_focus_ = nullptr;
  if(event->button() == Qt::LeftButton) {
    QApplication::restoreOverrideCursor();
  }
}

void TimelineView::copy() {
  QString t = selection_text();
  if(!t.isEmpty()) {
    QApplication::clipboard()->setText(t);
    QApplication::clipboard()->setText(t, QClipboard::Selection);
  }
}

static float point_rect_dist(const QPointF &p, const QRectF &r) {
  auto cx = std::max(std::min(p.x(), r.right()), r.left());
  auto cy = std::max(std::min(p.y(), r.bottom()), r.top());
  auto dx = p.x() - cx;
  auto dy = p.y() - cy;
  return std::sqrt(dx*dx + dy*dy);
}

TimelineView::VisibleBlock *TimelineView::block_near(const QPointF &p) {
  float minimum_distance = std::numeric_limits<float>::max();
  VisibleBlock *closest = nullptr;
  for(auto &x : visible_blocks_) {
    float dist = point_rect_dist(p, x.bounds);
    if(dist > minimum_distance) return closest;
    closest = &x;
    minimum_distance = dist;
  }
  return closest;
}

QString TimelineView::selection_text() const {
  QString result;
  SelectionScanner ss(selection_);
  for(auto block = blocks_.crbegin(); block != blocks_.crend(); ++block) {
    ss.advance(&*block);
    result = block->selection_text(fontMetrics(), ss.fully_selected(&*block), ss.start_point(), ss.end_point())
      + (result.isEmpty() ? "" : "\n") + result;

    if(ss.ending_selection()) {
      return result;
    }
  }

  qDebug() << "Couldn't find selection end!";

  return result;
}

QSize TimelineView::sizeHint() const {
  auto metrics = fontMetrics();
  return QSize(metrics.width('x')*80 + block_info().avatar_size() + verticalScrollBar()->sizeHint().width(), 2*metrics.lineSpacing());
}

QSize TimelineView::minimumSizeHint() const {
  auto metrics = fontMetrics();
  return QSize(metrics.width('x')*20 + block_info().avatar_size() + verticalScrollBar()->sizeHint().width(), 2*metrics.lineSpacing());
}

void TimelineView::focusOutEvent(QFocusEvent *e) {
  dispatch_event({}, e);
  grabbed_focus_ = nullptr;

  // Taken from QWidgetTextControl
  if(e->reason() != Qt::ActiveWindowFocusReason
     && e->reason() != Qt::PopupFocusReason) {
    selection_ = {};
    viewport()->update();
  }
}

void TimelineView::contextMenuEvent(QContextMenuEvent *event) {
  dispatch_event(QPointF(event->pos()), event);
}

bool TimelineView::viewportEvent(QEvent *e) {
  if(e->type() == QEvent::ToolTip) {
    auto help = static_cast<QHelpEvent*>(e);
    dispatch_event(QPointF(help->pos()), help);
    if(!help->isAccepted()) {
      QToolTip::hideText();
    }

    return true;
  }
  return QAbstractScrollArea::viewportEvent(e);
}

void TimelineView::ref_avatar(const matrix::Content &content) {
  auto result = avatars_.emplace(std::piecewise_construct,
                                 std::forward_as_tuple(content),
                                 std::forward_as_tuple());
  if(result.second) {
    // New avatar, download it
    const auto size = block_info().avatar_size();
    auto reply = room_.session().get_thumbnail(result.first->first, devicePixelRatioF() * QSize(size, size));
    connect(reply, &matrix::ContentFetch::finished, this, &TimelineView::set_avatar);
    connect(reply, &matrix::ContentFetch::error, &room_, &matrix::Room::error);
  }
  ++result.first->second.references;
}

TimelineView::VisibleBlock *TimelineView::dispatch_event(const optional<QPointF> &p, QEvent *e) {
  VisibleBlock *b = p ? block_near(*p) : nullptr;
  if(grabbed_focus_) {
    grabbed_focus_->event(room_, *viewport(), block_info(),
                          (p && b->block == grabbed_focus_) ? *p - b->bounds.topLeft() : optional<QPointF>{},
                          e);
  } else if(b && b->bounds.contains(*p)) {
    b->block->event(room_, *viewport(), block_info(), *p - b->bounds.topLeft(), e);
  }

  return b;
}

void TimelineView::push_error(const QString &msg) {
  qDebug() << "ERROR:" << msg;
}
