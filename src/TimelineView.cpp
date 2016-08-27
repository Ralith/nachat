#include "TimelineView.hpp"

#include <cmath>
#include <vector>
#include <sstream>
#include <iomanip>
#include <stdexcept>

#include <QShortcut>
#include <QPainter>
#include <QScrollBar>
#include <QRegularExpression>
#include <QStringBuilder>
#include <QGuiApplication>
#include <QClipboard>
#include <QToolTip>

#include <QDebug>

#include "matrix/Room.hpp"

#include "Spinner.hpp"

using std::experimental::optional;

namespace {

constexpr std::chrono::minutes BLOCK_MERGE_INTERVAL(5);

qreal block_spacing(const QWidget &parent) {
  return std::round(parent.fontMetrics().lineSpacing() * 0.75);
}

qreal block_padding(const QWidget &parent) {
  return std::round(parent.fontMetrics().lineSpacing() * 0.33);
}

auto to_time_point(uint64_t ts) {
  return std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::from_time_t(0))
      + std::chrono::duration<uint64_t, std::milli>(ts);
}

QString to_timestamp(const char *format, Time p) {
  auto time = std::chrono::system_clock::to_time_t(p);
  auto tm = std::localtime(&time);
  std::ostringstream s;
  s << std::put_time(tm, format);
  return QString::fromStdString(s.str());
}

QString pretty_size(double n) {
  constexpr const static char *const units[9] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB"}; // should be enough for anyone!
  auto idx = std::min<size_t>(8, std::log(n)/std::log(1024.));
  return QString::number(n / std::pow<double>(1024, idx), 'g', 4) + " " + units[idx];
}

void href_urls(const QPalette &palette, QVector<QTextLayout::FormatRange> &formats, const QString &text, int offset = 0) {
  const static QRegularExpression regex(
    R"(\b()"
    R"([a-z][a-z0-9+-.]*://[^\s]+)"
    R"(|[^\s]+\.(com|net|org)(/[^\s]*)?)"
    R"(|www\.[^\s]+\.[^\s]+)"
    R"(|data:[^\s]+)"
    R"())",
    QRegularExpression::UseUnicodePropertiesOption | QRegularExpression::OptimizeOnFirstUsageOption | QRegularExpression::CaseInsensitiveOption);

  auto urls = regex.globalMatch(text, offset);
  while(urls.hasNext()) {
    auto candidate = urls.next();
    // QUrl doesn't handle some things consistently (e.g. emoticons in .la) so we round-trip it
    QUrl url(QUrl(candidate.captured(), QUrl::StrictMode).toString(QUrl::FullyEncoded), QUrl::StrictMode);
    if(!url.isValid()) continue;
    if(url.scheme().isEmpty()) url = QUrl("http://" + url.toString(QUrl::FullyEncoded), QUrl::StrictMode);

    QTextLayout::FormatRange range;
    range.start = candidate.capturedStart();
    range.length = candidate.capturedLength();
    QTextCharFormat format;
    format.setAnchor(true);
    format.setAnchorHref(url.toString(QUrl::FullyEncoded));
    format.setForeground(palette.link());
    format.setFontUnderline(true);
    range.format = format;
    formats.push_back(range);
  }
}

QVector<QTextLayout::FormatRange> format_view(const QVector<QTextLayout::FormatRange> &formats, int start, int length) {
  const auto end = start + length;
  QVector<QTextLayout::FormatRange> result;
  for(const auto &in : formats) {
    const auto in_end = in.start + in.length;
    if(in_end <= start || end <= in.start) continue;
    QTextLayout::FormatRange out;
    out.format = in.format;
    out.start = in.start - start;
    out.length = in.length;
    result.push_back(out);
  }
  return result;
}

optional<matrix::UserID> get_affected_user(const matrix::event::Room &e) {
  if(e.type() != matrix::event::room::Member::tag()) return {};
  matrix::event::room::Member member_evt{matrix::event::room::State{e}};
  return member_evt.user();
}

}

EventLike::EventLike(const matrix::RoomState &state, matrix::event::Room real)
  : EventLike(state, real.sender(), to_time_point(real.origin_server_ts()), real.type(), real.content(), get_affected_user(real))
{
  event = std::move(real);
}

EventLike::EventLike(const matrix::RoomState &state, const matrix::UserID &sender, Time time, matrix::EventType type, matrix::event::Content content,
                     optional<matrix::UserID> affected_user)
  : type{std::move(type)}, time{time}, sender{sender}, content{std::move(content)}
{
  if(affected_user) {
    auto m = state.member_from_id(*affected_user);
    affected_user_info = MemberInfo{*affected_user, m ? m->content() : matrix::event::room::MemberContent::leave};
  }

  auto member = state.member_from_id(sender);
  if(member) {
    disambiguation = state.member_disambiguation(*member);
    member_content = member->content();
  }
}

optional<matrix::event::room::MemberContent> EventLike::effective_profile() const {
  // Events concerning non-present users use the profile they set, whereas all others use the previously set one, if any
  if(affected_user_info && affected_user_info->user == sender) {
    matrix::event::room::MemberContent mc{content};
    if(affected_user_info->prev_content.membership() == matrix::Membership::LEAVE
       || affected_user_info->prev_content.membership() == matrix::Membership::BAN) {
      return mc;
    }
  }
  return member_content;
}

void EventLike::redact(const matrix::event::room::Redaction &because) {
  if(!event) throw std::logic_error("tried to redact a fake event");
  event->redact(because);
  time = {};
  content = event->content();
}

EventBlock::EventBlock(TimelineView &parent, ThumbnailCache &thumbnail_cache, gsl::span<const EventLike *const> events)
  : parent_{parent}, sender_{events[0]->sender}, events_{static_cast<std::size_t>(events.size())}
{
  const auto &front = *events[0];

  if(auto p = front.effective_profile()) {
    if(auto avatar = p->avatar_url()) {
      const auto size = static_cast<int>(std::floor(avatar_extent()));
      try {
        avatar_ = ThumbnailRef{matrix::Thumbnail{matrix::Content{*avatar}, QSize{size, size}, matrix::ThumbnailMethod::SCALE}, thumbnail_cache};
      } catch(const matrix::illegal_content_scheme &) {
        qDebug() << "illegal content in avatar url" << *avatar << "for user" << front.sender.value();
      }
    }
  }

  if(front.time) {
    time_ = TimeInfo{*events[0]->time, *events[events.size()-1]->time};
  }

  {
    QTextOption options;
    options.setAlignment(Qt::AlignLeft | Qt::AlignTop);
    options.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

    optional<QString> displayname;
    if(auto p = front.effective_profile()) {
      displayname = p->displayname();
    }
    name_.setText((displayname ? *displayname : front.sender.value())
                  + (front.disambiguation ? QString(" (" % *front.disambiguation % ")") : ""));
    name_.setFont(parent_.font());
    name_.setTextOption(options);
    name_.setCacheEnabled(true);
  }

  {
    QTextOption options;
    options.setAlignment(Qt::AlignRight | Qt::AlignTop);
    options.setWrapMode(QTextOption::NoWrap);
    timestamp_.setFont(parent_.font());
    timestamp_.setTextOption(options);
    timestamp_.setCacheEnabled(true);
  }

  for(std::size_t i = 0; i < events_.size(); ++i) {
    events_[i].init(parent, *this, *events[i]);
  }
}

void EventBlock::update_layout(qreal width) {
  const auto &metrics = parent_.fontMetrics();

  // Header and first line
  const qreal early_offset = avatar_extent() + horizontal_padding();

  size_t lines = 0;
  {
    name_.beginLayout();
    while(true) {
      auto line = name_.createLine();
      if(!line.isValid()) break;
      qreal offset = (lines < 2) * early_offset;
      line.setLineWidth(width - offset);
      line.setPosition(QPointF(offset, lines * metrics.lineSpacing()));
      lines += 1;
    }
    name_.endLayout();
  }

  {
    // Lay out as a single or range timestamp as appropriate, degrading to single or nothing of space in the header is unavailable
    auto layout_ts = [&](){
      timestamp_.beginLayout();
      auto line = timestamp_.createLine();
      line.setLineWidth(width - early_offset);
      line.setPosition(QPointF(early_offset, 0));
      timestamp_.endLayout();
      if(name_.lineAt(0).naturalTextWidth() + early_offset > width - line.naturalTextWidth()) {
        timestamp_.clearLayout();
        return false;
      }
      return true;
    };
    if(time_) {
      auto start_ts = to_timestamp("%H:%M", time_->start);
      bool done = false;
      if(time_->end - time_->start > BLOCK_MERGE_INTERVAL) {
        auto end_ts = to_timestamp("%H:%M", time_->end);
        timestamp_.setText(start_ts % "–" % end_ts);
        done = layout_ts();
      }
      if(!done) {
        timestamp_.setText(start_ts);
        layout_ts();
      }
    } else {
      timestamp_.setText(TimelineView::tr("REDACTED"));
      layout_ts();
    }
  }

  for(auto &event : events_) {
    for(auto &paragraph : event.paragraphs) {
      paragraph.beginLayout();
      while(true) {
        auto line = paragraph.createLine();
        if(!line.isValid()) break;
        qreal offset = (lines < 2) * early_offset;
        line.setLineWidth(width - offset);
        line.setPosition(QPointF(offset, lines * metrics.lineSpacing()));
        lines += 1;
      }
      paragraph.endLayout();
    }
  }
}

QRectF EventBlock::bounds() const {
  // We assume that name_ overlaps timestamp_ and that all paragraphs have equal width.
  //return QRectF(0, 0, avatar_extent(), avatar_extent()) | name_.boundingRect() | events_.back().paragraphs.back().boundingRect();
  size_t lines = name_.lineCount();
  for(const auto &event : events_) {
    for(const auto &paragraph : event.paragraphs) {
      lines += paragraph.lineCount();
    }
  }
  return QRectF(0, 0, name_.boundingRect().width(), (std::max<size_t>(2, lines) - 1) * parent_.fontMetrics().lineSpacing() + parent_.fontMetrics().ascent());
}

void EventBlock::draw(QPainter &p, Selection *selection) const {
  if(avatar_) {
    if(const auto &pixmap = **avatar_) {
      const QSize logical_size = pixmap->size() / pixmap->devicePixelRatio();
      p.drawPixmap(QPointF((avatar_extent() - logical_size.width()) / 2.,
                           (avatar_extent() - logical_size.height()) / 2.),
                   *pixmap);
    } else {
      // TODO: Draw loading indicator
    }
  } else {
    // TODO: Draw default avatar
  }

  const static QPointF origin{0, 0};

  name_.draw(&p, origin);
  timestamp_.draw(&p, origin);

  for(const auto &event : events_) {
    for(const auto &paragraph : event.paragraphs) {
      paragraph.draw(&p, origin);
    }
  }
}

qreal EventBlock::avatar_extent() const {
  const auto m = parent_.fontMetrics();
  // From 0 to baseline of second line of text, so text flowed underneath isn't cramped
  return m.lineSpacing() + m.ascent();
}

qreal EventBlock::horizontal_padding() const {
  return std::round(parent_.fontMetrics().lineSpacing() * 0.33);
}

void EventBlock::Event::init(const TimelineView &view, const EventBlock &block, const EventLike &e) {
  source = e.event;

  const auto &&tr = [](const char *s) { return TimelineView::tr(s); };

  QString text;
  QVector<QTextLayout::FormatRange> formats;

  using namespace matrix::event::room;

  optional<QString> redaction;
  if(e.event) {
    redaction = e.event->redacted_because();
  }

  if(e.type == Message::tag()) {
    MessageContent msg{e.content};
    if(redaction) {
      if(*redaction != "") {
        text = tr("REDACTED: %1").arg(*redaction);
      } else {
        text = tr("REDACTED");
      }
    } else if(msg.type() == message::Text::tag() || msg.type() == message::Notice::tag()) {
      text = msg.body();
      href_urls(view.palette(), formats, text);
    } else if(msg.type() == message::Emote::tag()) {
      text = QString("* %1 %2").arg(block.name_.text()).arg(msg.body());
      href_urls(view.palette(), formats, text, block.name_.text().size() + 3);
    } else {
      qDebug() << "displaying fallback for unrecognized msgtype:" << msg.type().value();
      text = msg.body();
      href_urls(view.palette(), formats, text);
    }
  } else if(e.type == Member::tag()) {
    const MemberContent content{e.content};
    const MemberContent prev_content{e.affected_user_info->prev_content};
    const matrix::UserID &user = e.affected_user_info->user;
    if(user == block.sender_) {
      switch(content.membership()) {
      case matrix::Membership::INVITE:
        text = tr("invited themselves");
        break;
      case matrix::Membership::JOIN:
        switch(prev_content.membership()) {
        case matrix::Membership::INVITE:
          text = tr("accepted invite");
          break;
        case matrix::Membership::JOIN:
          if(content.avatar_url() != prev_content.avatar_url()) {
            if(content.displayname() != prev_content.displayname()) {
              if(content.displayname()) {
                text = tr("changed avatar and set display name to \"%1\"").arg(*content.displayname());
              } else {
                text = tr("changed avatar and removed display name");
              }
            } else {
              text = tr("changed avatar");
            }
          } else if(content.displayname() != prev_content.displayname()) {
            if(content.displayname()) {
              text = tr("set display name to \"%1\"").arg(*content.displayname());
            } else {
              text = tr("removed display name");
            }
          } else {
            text = "sent a no-op join";
          }
          break;
        default:
          text = tr("joined");
          break;
        }
        break;
      case matrix::Membership::LEAVE:
        text = tr("left");
        break;
      case matrix::Membership::BAN:
        text = tr("banned themselves");
        break;
      }
    } else {
      const QString pretty_target = content.displayname().value_or(user.value()); // TODO: Clickable
      switch(content.membership()) {
      case matrix::Membership::INVITE:
        text = tr("invited %1").arg(pretty_target);
        break;
      case matrix::Membership::JOIN:
        if(prev_content.membership() == matrix::Membership::JOIN) {
          text = tr("modified profile of %1").arg(pretty_target);
        } else {
          text = tr("forced %1 to join").arg(pretty_target);
        }
        break;
      case matrix::Membership::LEAVE:
        switch(prev_content.membership()) {
        case matrix::Membership::INVITE:
          text = tr("rescinded invite to %1").arg(pretty_target);
          break;
        case matrix::Membership::BAN:
          text = tr("unbanned %1").arg(pretty_target);
          break;
        default:
          text = tr("kicked %1").arg(pretty_target);
          break;
        }
      case matrix::Membership::BAN:
        text = tr("banned %1").arg(pretty_target);
        break;
      }
    }
    if(redaction) {
      if(*redaction != "") {
        text += tr(" (redacted: %1)").arg(*redaction);
      } else {
        text += tr(" (redacted)");
      }
    }
  } else if(e.type == Name::tag()) {
    const auto n = NameContent{e.content}.name();
    if(n) {
      text = tr("set the room name to \"%1\"").arg(*n);
    } else {
      text = tr("removed the room name");
    }
    if(redaction) {
      if(*redaction != "") {
        text += tr(" (redacted: %1)").arg(*redaction);
      } else {
        text += tr(" (redacted)");
      }
    }
  } else if(e.type == Create::tag()) {
    text = tr("created the room");
  } else {
    text = tr("unrecognized message type %1").arg(e.type.value());
  }

  const static QRegularExpression line_re("\\R", QRegularExpression::UseUnicodePropertiesOption | QRegularExpression::OptimizeOnFirstUsageOption);
  auto lines = text.split(line_re);


  QTextOption body_options;
  body_options.setAlignment(Qt::AlignLeft | Qt::AlignTop);
  body_options.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

  paragraphs = FixedVector<QTextLayout>(lines.size());
  size_t start = 0;
  for(int i = 0; i < lines.size(); ++i) {
    auto &paragraph = paragraphs[i];

    paragraph.setText(lines[i]);
    paragraph.setFormats(format_view(formats, start, lines[i].length()));
    paragraph.setFont(view.font());
    paragraph.setTextOption(body_options);
    paragraph.setCacheEnabled(true);

    start += 1 + lines[i].size();
  }
}

TimelineView::TimelineView(ThumbnailCache &cache, QWidget *parent)
  : QAbstractScrollArea{parent}, thumbnail_cache_{cache}, copy_{new QShortcut(QKeySequence::Copy, this)},
    at_bottom_{false} {
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  verticalScrollBar()->setSingleStep(20);  // Taken from QScrollArea
  setMouseTracking(true);

  QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  policy.setHorizontalStretch(1);
  policy.setVerticalStretch(0);
  setSizePolicy(policy);

  connect(verticalScrollBar(), &QAbstractSlider::valueChanged, [this]() {
      maybe_need_forwards();
      maybe_need_backwards();
    });
  connect(copy_, &QShortcut::activated, this, &TimelineView::copy);

  {
    const int extent = devicePixelRatioF() * spinner_space() * .9;
    spinner_ = QPixmap(extent, extent);
    spinner_.fill(Qt::transparent);
    QPainter painter(&spinner_);
    painter.setRenderHint(QPainter::Antialiasing);
    Spinner::paint(palette().color(QPalette::Shadow), palette().color(QPalette::Base), painter, extent);
    spinner_.setDevicePixelRatio(devicePixelRatioF());
  }
}

void TimelineView::prepend(const matrix::TimelineCursor &begin, const matrix::RoomState &state, const matrix::event::Room &evt) {
  if(!batches_.empty() && batches_.front().begin == begin) {
    batches_.front().events.emplace_front(state, evt);
  } else {
    batches_.emplace_front(begin, std::deque<EventLike>{EventLike{state, evt}});
  }

  if(auto txid = evt.transaction_id()) {
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                  [&](const Pending &p) { return p.transaction == *txid; }),
                   pending_.end());
  }

  rebuild_blocks();
  maybe_need_backwards();
}

void TimelineView::append(const matrix::TimelineCursor &begin, const matrix::RoomState &state, const matrix::event::Room &evt) {
  if(!batches_.empty() && batches_.back().begin == begin) {
    batches_.back().events.emplace_back(state, evt);
  } else {
    batches_.emplace_back(begin, std::deque<EventLike>{EventLike{state, evt}});
  }

  if(auto txid = evt.transaction_id()) {
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                  [&](const Pending &p) { return p.transaction == *txid; }),
                   pending_.end());
  }

  rebuild_blocks();
  maybe_need_forwards();
}

void TimelineView::redact(const matrix::event::room::Redaction &redaction) {
  bool done = false;
  for(auto &batch : batches_) {
    for(auto &existing_event : batch.events) {
      if(existing_event.event->id() == redaction.redacts()) {
        existing_event.redact(redaction);
        done = true;
      }
      if(done) break;
    }
    if(done) break;
  }

  rebuild_blocks();
  maybe_need_forwards();
  maybe_need_backwards();
}

void TimelineView::set_at_bottom(bool value) {
  at_bottom_ = value;
}

void TimelineView::resizeEvent(QResizeEvent *) {
  update_layout();
}

void TimelineView::paintEvent(QPaintEvent *) {
  const qreal spacing = block_spacing(*this);
  const qreal half_spacing = std::round(spacing * 0.5);
  const qreal padding = block_padding(*this);
  const auto view = view_rect();

  QPainter painter(viewport());
  painter.fillRect(viewport()->contentsRect(), palette().color(QPalette::Dark));
  painter.setPen(palette().color(QPalette::Text));
  painter.translate(QPointF(0, -view.top()));

  bool animating = false;
  if(view.bottom() > 0 && !at_bottom_) {
    draw_spinner(painter, 0);
    animating = true;
  }

  for(auto block = blocks_.crbegin(); block != blocks_.crend(); ++block) {
    const auto &bounds = block->bounds();
    painter.translate(QPointF(0, -std::round(spacing + bounds.height())));
    const auto block_top = painter.worldTransform().m32() + view.top();
    if(block_top > view.bottom()) continue;

    {
      const QRectF outline(0, 0, view.width(), bounds.height() + spacing);
      painter.save();
      painter.setRenderHint(QPainter::Antialiasing);
      QPainterPath path;
      path.addRoundedRect(outline, padding*2, padding*2);
      painter.fillPath(path, palette().color(QPalette::Base));
      painter.restore();
    }

    {
      painter.save();
      painter.translate(QPointF(padding, half_spacing));
      block->draw(painter, nullptr);
      painter.restore();
    }

    if(block_top < view.top()) break;
  }

  const auto top = painter.worldTransform().m32() + view.top();
  if(view.top() < top && !at_top()) {
    draw_spinner(painter, -spinner_space());
    animating = true;
  }

  if(animating) {
    QTimer::singleShot(30, viewport(), static_cast<void (QWidget::*)()>(&QWidget::update));
  }

}

void TimelineView::changeEvent(QEvent *) {
  // Optimization: Block lifecycle could be refactored to construct/polish/flow instead of construct/flow to reduce CPU use
  rebuild_blocks();
}

void TimelineView::mousePressEvent(QMouseEvent *event) {
  // TODO

  if(event->button() == Qt::LeftButton) {
    QGuiApplication::setOverrideCursor(Qt::IBeamCursor);
    selection_.begin = event->localPos();
    selection_.end = selection_.begin;
    viewport()->update();
  }
}

void TimelineView::mouseMoveEvent(QMouseEvent *event) {
  // TODO

  if(event->buttons() & Qt::LeftButton) {
    selection_.end = event->localPos();
    viewport()->update();

    QString t = selection_text();
    if(!t.isEmpty())
      QGuiApplication::clipboard()->setText(selection_text(), QClipboard::Selection);
  }
}

void TimelineView::mouseReleaseEvent(QMouseEvent *event) {
  // TODO

  if(event->button() == Qt::LeftButton) {
    QGuiApplication::restoreOverrideCursor();
  }
}

void TimelineView::focusOutEvent(QFocusEvent *e) {
  // Taken from QWidgetTextControl
  if(e->reason() != Qt::ActiveWindowFocusReason
     && e->reason() != Qt::PopupFocusReason) {
    selection_ = {QPointF(), QPointF()};
    viewport()->update();
  }
}

void TimelineView::contextMenuEvent(QContextMenuEvent *event) {
  // TODO
}

bool TimelineView::viewportEvent(QEvent *e) {
  if(e->type() == QEvent::ToolTip) {
    auto help = static_cast<QHelpEvent*>(e);
    // TODO
    if(!help->isAccepted()) {
      QToolTip::hideText();
    }

    return true;
  }
  return QAbstractScrollArea::viewportEvent(e);
}

QString TimelineView::selection_text() const {
  return "TODO";
}

void TimelineView::copy() const {
  QString t = selection_text();
  if(!t.isEmpty()) {
    QGuiApplication::clipboard()->setText(t);
    QGuiApplication::clipboard()->setText(t, QClipboard::Selection);
  }
}

QRectF TimelineView::view_rect() const {
  const auto &r = viewport()->contentsRect();
  return r.translated(0, -r.height() - (verticalScrollBar()->maximum() - verticalScrollBar()->value()) + !at_bottom_ * spinner_space());
}

void TimelineView::update_scrollbar(int content_height) {
  auto &scroll = *verticalScrollBar();
  const bool was_at_bottom = scroll.value() == scroll.maximum();
  const auto view_height = viewport()->contentsRect().height();

  content_height += (!at_bottom_ + !at_top()) * spinner_space();

  scroll.setMaximum(content_height > view_height ? content_height - view_height : 0);
  scroll.setPageStep(viewport()->contentsRect().height());
}

// Whether two events should be assigned to distinct blocks
static bool block_border(const EventLike &a, const EventLike &b) {
  return b.sender != a.sender || !b.time || !a.time || *b.time - *a.time > BLOCK_MERGE_INTERVAL;
}

void TimelineView::rebuild_blocks() {
  // Optimization: defer until visible
  blocks_.clear();
  std::vector<const EventLike *> block_events;
  for(const auto &batch : batches_) {
    for(const auto &event : batch.events) {
      if(!block_events.empty() && block_border(*block_events.back(), event)) {
        blocks_.emplace_back(*this, thumbnail_cache_, block_events);
        block_events.clear();
      }
      block_events.emplace_back(&event);
    }
  }
  if(at_bottom_) {
    for(const auto &event : pending_) {
      if(!block_events.empty() && block_border(*block_events.back(), event.event)) {
        blocks_.emplace_back(*this, thumbnail_cache_, block_events);
        block_events.clear();
      }
      block_events.emplace_back(&event.event);
    }
  }
  if(!block_events.empty()) {
    blocks_.emplace_back(*this, thumbnail_cache_, block_events);
    block_events.clear();
  }
  update_layout();
}

void TimelineView::update_layout() {
  ensurePolished();

  qreal content_height = blocks_.size() * block_spacing(*this);

  const auto width = viewport()->contentsRect().width() - 2*block_padding(*this);
  for(auto &block : blocks_) {
    block.update_layout(width);
    content_height += block.bounds().height();
  }

  update_scrollbar(content_height);

  viewport()->update();
}

void TimelineView::maybe_need_backwards() {
  if(at_top()) return;
  need_backwards();
}

void TimelineView::maybe_need_forwards() {
  if(at_bottom_) return;
  need_forwards();
}

bool TimelineView::at_top() const {
  return !batches_.empty() && batches_.front().events.front().type == matrix::event::room::Create::tag();
}

qreal TimelineView::spinner_space() const {
  return fontMetrics().lineSpacing() * 4;
}

void TimelineView::draw_spinner(QPainter &painter, qreal top) const {
  const qreal extent = spinner_.width() / spinner_.devicePixelRatio();
  painter.save();

  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  painter.translate(view_rect().width() * 0.5, top + spinner_space() * 0.5);
  auto t = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now());
  constexpr qreal rotation_seconds = 2;
  const qreal angle = 360. * static_cast<qreal>(t.time_since_epoch().count() % static_cast<uint64_t>(1000 * rotation_seconds)) / (1000 * rotation_seconds);
  painter.rotate(angle);
  painter.drawPixmap(QPointF(-extent * 0.5, -extent * 0.5), spinner_);

  painter.restore();
}
