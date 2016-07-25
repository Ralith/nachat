#include "TimelineView.hpp"

#include <sstream>
#include <iomanip>
#include <ctime>

#include <QDebug>
#include <QScrollBar>
#include <QPainter>
#include <QShortcut>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QMenu>
#include <QRegularExpression>
#include <QTimer>

#include "matrix/Session.hpp"
#include "Spinner.hpp"
#include "RedactDialog.hpp"

using std::experimental::optional;

constexpr static int BACKLOG_BATCH_SIZE = 50;
constexpr static std::chrono::minutes BLOCK_MERGE_INTERVAL(2);
constexpr static std::chrono::minutes TIMESTAMP_RANGE_THRESHOLD(2);

static auto to_time_point(uint64_t ts) {
  return std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::from_time_t(0))
      + std::chrono::duration<uint64_t, std::milli>(ts);
}

static QString pretty_size(double n) {
  constexpr const static char *const units[9] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB"}; // should be enough for anyone!
  uint64_t idx = std::min<size_t>(8, std::log(n)/std::log(1024.));
  return QString::number(n / std::pow<double>(1024, idx), 'g', 4) + " " + units[idx];
}

std::vector<std::pair<QString, QVector<QTextLayout::FormatRange>>>
  TimelineView::format_text(const matrix::RoomState &state, const matrix::proto::Event &evt, const QString &str) const {

  std::vector<std::pair<QString, QVector<QTextLayout::FormatRange>>> result;
  const static QRegularExpression line_re("\\R", QRegularExpression::UseUnicodePropertiesOption | QRegularExpression::OptimizeOnFirstUsageOption);
  auto lines = str.split(line_re);
  result.reserve(lines.size());
  std::transform(lines.begin(), lines.end(), std::back_inserter(result),
                 [](const QString &s){ return std::pair<QString, QVector<QTextLayout::FormatRange>>(s, {}); });

  if(evt.type == "m.room.message") {
    const auto msgtype = evt.content["msgtype"].toString();
    if(msgtype == "m.emote" && !result.empty()) {
      auto &member = *state.member_from_id(evt.sender);
      result.front().first = "* " % member.pretty_name() % " " % result.front().first;
    }
  }

  // Detect highlights
  auto self = state.member_from_id(room_.session().user_id());
  const bool highlight = self && str.toCaseFolded().contains(self->display_name().toCaseFolded());
  if(highlight) {
    QTextCharFormat highlight_format;
    highlight_format.setFontWeight(QFont::Bold);
    for(auto &line : result) {
      QTextLayout::FormatRange range;
      range.start = 0;
      range.length = line.first.length();
      range.format = highlight_format;
      line.second.push_back(range);
    }
  }

  // Detect URLs
  const static QRegularExpression maybe_url_re(
    "("
    R"([a-z][a-z0-9+-.]*://[^\s]+)"
    R"(|[^\s]+\.(com|net|org)(/[^\s]*)?)"
    R"(|www\.[^\s]+\.[^\s]+)"
    ")",
    QRegularExpression::UseUnicodePropertiesOption | QRegularExpression::OptimizeOnFirstUsageOption | QRegularExpression::CaseInsensitiveOption);
  for(auto &line : result) {
    auto urls = maybe_url_re.globalMatch(line.first);
    while(urls.hasNext()) {
      auto candidate = urls.next();
      // QUrl doesn't handle some things consistently (e.g. emoticons in .la) so we round-trip it
      QUrl url(QUrl(candidate.captured(), QUrl::StrictMode).toString(QUrl::FullyEncoded), QUrl::StrictMode);
      if(!url.isValid()) continue;
      if(url.scheme().isEmpty()) url = QUrl("https://" + url.toString(QUrl::FullyEncoded), QUrl::StrictMode);

      QTextLayout::FormatRange range;
      range.start = candidate.capturedStart();
      range.length = candidate.capturedLength();
      QTextCharFormat format;
      format.setAnchor(true);
      format.setAnchorHref(url.toString(QUrl::FullyEncoded));
      format.setForeground(palette().link());
      format.setFontUnderline(true);
      range.format = format;
      line.second.push_back(range);
    }
  }
  return result;
}

TimelineView::Event::Event(const TimelineView &view, const matrix::RoomState &state, const matrix::proto::Event &e)
  : data(e), time(to_time_point(e.origin_server_ts)) {
  std::vector<std::pair<QString, QVector<QTextLayout::FormatRange>>> lines;
  if(e.type == "m.room.message") {
    const auto msgtype = e.content["msgtype"].toString();
    if(msgtype == "m.file" || msgtype == "m.image" || msgtype == "m.video" || msgtype == "m.audio") {
      lines.emplace_back();
      auto &line = lines.front();
      line.first = e.content["filename"].toString();
      {
        QTextLayout::FormatRange range;
        range.start = 0;
        range.length = line.first.length();
        QTextCharFormat format;
        format.setAnchor(true);
        format.setAnchorHref(e.content["url"].toString());
        format.setForeground(view.palette().link());
        format.setFontUnderline(true);
        range.format = format;
        line.second.push_back(range);
      }
      auto info = e.content["info"].toObject();
      auto type = info["mimetype"];
      auto size = info["size"];
      if(type.isString() || size.isDouble())
        line.first += " (";
      if(size.isDouble())
        line.first += pretty_size(size.toDouble());
      if(type.isString())
        line.first += " " % type.toString();
      if(type.isString() || size.isDouble())
        line.first += ")";
    } else {
      lines = view.format_text(state, e, e.content["body"].toString());
    }
  } else if(e.type == "m.room.member") {
    switch(matrix::parse_membership(e.content["membership"].toString()).value()) {
    case matrix::Membership::INVITE: {
      auto invitee = state.member_from_id(e.state_key);
      if(!invitee) {
        qDebug() << "got invite for non-member" << e.state_key << " probably because we're probably stepping backwards over a duplicated event from SYN-645";
        lines = view.format_text(state, e, tr("SYN-645 related error"));
      } else {
        lines = view.format_text(state, e, tr("invited %1").arg(state.member_name(*invitee)));
      }
      break;
    }
    case matrix::Membership::JOIN: {
      if(!e.unsigned_.prev_content) {
        lines = view.format_text(state, e, tr("joined"));
        break;
      }
      const auto &prev = *e.unsigned_.prev_content;
      auto prev_membership = matrix::parse_membership(prev["membership"].toString()).value();
      switch(prev_membership) {
      case matrix::Membership::INVITE:
        lines = view.format_text(state, e, tr("accepted invite"));
        break;
      case matrix::Membership::JOIN: {
        const bool avatar_changed = QUrl(prev["avatar_url"].toString()) != QUrl(e.content["avatar_url"].toString());
        const auto new_dn = e.content["displayname"].toString();
        const auto old_dn = prev["displayname"].toString();
        const bool dn_changed = old_dn != new_dn;
        if(avatar_changed && dn_changed) {
          if(new_dn.isEmpty())
            lines = view.format_text(state, e, tr("unset display name and changed avatar"));
          else
            lines = view.format_text(state, e, tr("changed display name to %1 and changed avatar").arg(new_dn));
        } else if(avatar_changed) {
          lines = view.format_text(state, e, tr("changed avatar"));
        } else if(dn_changed) {
          if(new_dn.isEmpty()) {
            lines = view.format_text(state, e, tr("unset display name"));
          } else if(old_dn.isEmpty()) {
            lines = view.format_text(state, e, tr("set display name to %1").arg(new_dn));
          } else {
            lines = view.format_text(state, e, tr("changed display name from %1 to %2").arg(old_dn).arg(new_dn));
          }
        } else {
          lines = view.format_text(state, e, tr("sent a no-op join"));
        }
        break;
      }
      default:
        lines = view.format_text(state, e, tr("joined"));
        break;
      }
      break;
    }
    case matrix::Membership::LEAVE: {
      lines = view.format_text(state, e, tr("left"));
      break;
    }
    case matrix::Membership::BAN: {
      auto banned = state.member_from_id(e.state_key);
      if(!banned) {
        qDebug() << "INTERNAL ERROR: displaying ban of unknown member" << e.state_key;
        lines = view.format_text(state, e, tr("banned %1").arg(e.state_key));
      } else {
        lines = view.format_text(state, e, tr("banned %1").arg(state.member_name(*banned)));
      }
      break;
    }
    }
  } else {
    lines = view.format_text(state, e, tr("unrecognized event type %1").arg(e.type));
  }
  layouts = std::vector<QTextLayout>(lines.size());
  QTextOption body_options;
  body_options.setAlignment(Qt::AlignLeft | Qt::AlignTop);
  body_options.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  for(size_t i = 0; i < lines.size(); ++i) {
    layouts[i].setFont(view.font());
    layouts[i].setTextOption(body_options);
    layouts[i].setCacheEnabled(true);
    layouts[i].setText(lines[i].first);
    layouts[i].setFormats(lines[i].second);
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

  {
    QTextOption options;
    options.setAlignment(Qt::AlignLeft | Qt::AlignTop);
    options.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    name_layout_.setFont(view.font());
    name_layout_.setTextOption(options);
    name_layout_.setCacheEnabled(true);
  }

  {
    QTextOption options;
    options.setAlignment(Qt::AlignRight | Qt::AlignTop);
    options.setWrapMode(QTextOption::NoWrap);
    timestamp_layout_.setFont(view.font());
    timestamp_layout_.setTextOption(options);
    timestamp_layout_.setCacheEnabled(true);
  }

  update_header(view, state);
}

void TimelineView::Block::update_header(TimelineView &view, const matrix::RoomState &state) {
  auto sender = state.member_from_id(sender_id_);
  if(sender && !sender->avatar_url().isEmpty()) {
    std::experimental::optional<matrix::Content> new_avatar;
    try {
      new_avatar = matrix::Content(sender->avatar_url());
    } catch(const std::invalid_argument &e) {
      qDebug() << sender_id_ << "in" << view.room_.pretty_name() << "has invalid avatar URL:" << e.what() << ":" << sender->avatar_url();
    }
    if(new_avatar != avatar_) {
      if(avatar_) view.unref_avatar(*avatar_);

      if(new_avatar) {
        avatar_ = std::move(*new_avatar);
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
  }
  if(sender) {
    auto name = sender->pretty_name();
    auto disambig = state.member_disambiguation(*sender);
    if(disambig.isEmpty()) {
      name_layout_.setText(name);
    } else {
      name_layout_.setText(name % " (" % disambig % ")");
      QTextLayout::FormatRange f;
      f.start = name.length() + 1;
      f.length = disambig.length() + 2;
      QTextCharFormat format;
      format.setFontWeight(QFont::Bold);
      f.format = format;
      name_layout_.setFormats({f});
    }
  } else {
    name_layout_.setText(sender_id_);
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

static optional<int> cursor_at(const QTextLayout &layout, const QPointF &p) {
  for(int i = 0; i < layout.lineCount(); ++i) {
    const auto line = layout.lineAt(i);
    if(line.naturalTextRect().contains(p)) {
      return line.xToCursor(p.x());
    }
  }
  return {};
}

static std::vector<QTextLayout::FormatRange> formats_at(const QTextLayout &layout, int cursor) {
  std::vector<QTextLayout::FormatRange> result;
  for(auto &format : layout.formats()) {
    if(format.start <= cursor && format.start + format.length > cursor) {
      result.push_back(format);
    }
  }
  return result;
}

optional<TimelineView::ClickTarget> TimelineView::Event::target_at(const QPointF &pos) {
  for(const auto &layout : layouts) {
    if(auto cursor = cursor_at(layout, pos)) {
      auto formats = formats_at(layout, *cursor);
      auto anchor = std::find_if(formats.begin(), formats.end(), [](const QTextLayout::FormatRange &f) { return f.format.isAnchor(); });
      if(anchor != formats.end()) {
        return ClickTarget{ClickTarget::Type::CONTENT_LINK, this, anchor->format.anchorHref(), &layout, anchor->start, anchor->length};
      }
      break;
    }
  }
  return ClickTarget{ClickTarget::Type::EVENT, this, QUrl(), nullptr, 0, 0};
}

QRectF TimelineView::Block::bounding_rect(const TimelineView &view) const {
  const auto metrics = view.fontMetrics();
  QRectF rect(0, 0, view.visible_width() - 2*view.block_margin(), view.avatar_size());
  rect |= name_layout_.boundingRect();
  QPointF offset(0, name_layout_.boundingRect().height() + metrics.leading());
  for(const auto event : events_) {
    auto event_bounds = event->bounding_rect();
    rect |= event_bounds.translated(offset);
    offset.ry() += event_bounds.height() + metrics.leading();
  }
  return rect;
}

enum class Accuracy { EXACT, NEAR };

static int cursor_near(const QTextLayout &layout, const QPointF &p) {
  int cursor;
  for(int i = 0; i < layout.lineCount(); ++i) {
    const auto line = layout.lineAt(i);
    const auto rect = line.rect();
    if(p.y() < rect.top()) {
      return line.xToCursor(rect.left());
    }
    if(p.y() > rect.top() && p.y() < rect.bottom()) {
      return line.xToCursor(p.x());
    }
    if(p.y() > rect.bottom()) {
      cursor = line.xToCursor(rect.right());
    }
    // TODO: Special handling for right-to-left text?
  }
  return cursor;
}

struct TextRange {
  int start, length;
};

static TextRange selection_for(const QTextLayout &layout, QPointF layout_origin, 
                               bool select_all, optional<QPointF> select_start, optional<QPointF> select_end) {
  TextRange f;
  if(select_all) {
    f.start = 0;
    f.length = layout.text().length();
  } else if(select_start && select_end) {
    auto start_cursor = cursor_near(layout, *select_start - layout_origin);
    auto end_cursor = cursor_near(layout, *select_end - layout_origin);
    if(start_cursor == end_cursor) {
      f.start = 0;
      f.length = 0;
    } else {
      f.start = std::min(start_cursor, end_cursor);
      f.length = std::max(start_cursor, end_cursor) - f.start;
    }
  } else if(select_start) {
    f.start = 0;
    f.length = cursor_near(layout, *select_start - layout_origin);
  } else if(select_end) {
    f.start = cursor_near(layout, *select_end - layout_origin);
    f.length = layout.text().length() - f.start;
  } else {
    f.start = 0;
    f.length = 0;
  }
  return f;
}

void TimelineView::Block::draw(const TimelineView &view, QPainter &p, QPointF offset, bool select_all,
                               optional<QPointF> select_start, optional<QPointF> select_end) const {
  auto metrics = view.fontMetrics();

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
    avatar_pixmap = view.avatar_unset_.pixmap(size, size);
  }
  p.drawPixmap(QPoint(offset.x() + view.block_margin() + (size - avatar_pixmap.width()) / 2,
                      offset.y() + (size - avatar_pixmap.height()) / 2),
               avatar_pixmap);

  QVector<QTextLayout::FormatRange> selections;
  selections.push_back(QTextLayout::FormatRange());
  const auto cg = view.hasFocus() ? QPalette::Active : QPalette::Inactive;
  selections.front().format.setBackground(view.palette().brush(cg, QPalette::Highlight));
  selections.front().format.setForeground(view.palette().brush(cg, QPalette::HighlightedText));
  auto &&sel = [&](const QTextLayout &l, QPointF o) {
    auto range = selection_for(l, o, select_all, select_start, select_end);
    selections.front().start = range.start;
    selections.front().length = range.length;
  };

  p.save();
  p.setPen(view.palette().color(QPalette::Dark));
  sel(name_layout_, QPointF(0, 0));
  name_layout_.draw(&p, offset, selections);
  sel(timestamp_layout_, QPointF(0, 0));
  timestamp_layout_.draw(&p, offset, selections);
  p.restore();

  QPointF local_offset(0, name_layout_.boundingRect().height() + metrics.leading());

  for(const auto event : events_) {
    p.save();
    if(event->data.type != "m.room.message") {
      p.setPen(view.palette().color(QPalette::Dark));
    }
    QRectF event_bounds;
    for(const auto &layout : event->layouts) {
      sel(layout, local_offset);
      layout.draw(&p, offset + local_offset, selections);
      event_bounds |= layout.boundingRect();
    }
    local_offset.ry() += event_bounds.height() + metrics.leading();
    p.restore();
  }
}

QString TimelineView::Block::selection_text(const QFontMetrics &metrics, bool select_all,
                                            optional<QPointF> select_start, optional<QPointF> select_end) const {
  QString result;
  auto &&get = [&](const QTextLayout &l, const QPointF &o) {
    const auto range = selection_for(l, o, select_all, select_start, select_end);
    return l.text().mid(range.start, range.length);
  };
  auto name_str = get(name_layout_, QPointF(0, 0));
  auto timestamp_str = get(timestamp_layout_, QPointF(0, 0));
  if(!name_str.isEmpty() && !timestamp_str.isEmpty()) {
    result = name_str % " - " % timestamp_str;
  } else {
    result = name_str + timestamp_str;
  }
  QPointF offset(0, name_layout_.boundingRect().height() + metrics.leading());
  for(const auto event : events_) {
    QRectF event_bounds;
    bool empty = true;
    for(const auto &layout : event->layouts) {
      auto str = get(layout, offset);
      if(!str.isEmpty()) {
        if(!result.isEmpty()) result += "\n  ";
        result += str;
      }
      empty &= str.isEmpty();
      event_bounds |= layout.boundingRect();
    }
    offset.ry() += event_bounds.height() + metrics.leading();
  }
  return result;
}

size_t TimelineView::Block::size() const {
  return events_.size();
}

TimelineView::EventHit TimelineView::Block::event_at(const QFontMetrics &metrics, const QPointF &p) {
  QPointF offset(0, name_layout_.boundingRect().height() + metrics.leading());
  for(const auto event : events_) {
    const auto rel_pos = p - offset;
    QRectF event_bounds;
    bool hit = false;
    for(const auto &layout : event->layouts) {
      hit |= layout.boundingRect().contains(rel_pos);
      event_bounds |= layout.boundingRect();
    }
    if(hit) {
      return EventHit{rel_pos, event};
    }
    offset.ry() += event_bounds.height() + metrics.leading();
  }
  return EventHit{QPointF(), nullptr};
}

void TimelineView::Block::hover(TimelineView &view, const QPointF &p) {
  auto target = target_at(view, p);
  if(target && target->type != ClickTarget::Type::EVENT) {
    view.viewport()->setCursor(Qt::PointingHandCursor);
  } else {
    const auto metrics = view.fontMetrics();
    if(p.x() > view.avatar_size() + 2*view.block_margin()) {
      if(p.y() < name_layout_.boundingRect().height() + metrics.leading() || event_at(metrics, p)) {
        view.viewport()->setCursor(Qt::IBeamCursor);
      } else {
        view.viewport()->setCursor(Qt::ArrowCursor);
      }
    } else {
      view.viewport()->setCursor(Qt::ArrowCursor);
    }
  }
}

optional<TimelineView::ClickTarget> TimelineView::Block::target_at(TimelineView &view, const QPointF &p) {
  if(avatar_ && QRectF(view.block_margin(), 0, view.avatar_size(), view.avatar_size()).contains(p)) {
    return ClickTarget{ClickTarget::Type::AVATAR, nullptr, avatar_->url(), nullptr, 0, 0};
  }
  if(auto e = event_at(view.fontMetrics(), p)) {
    return e.event->target_at(e.pos);
  }
  return {};
}

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
      menu_(new QMenu(this)) {
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
    const int extent = scrollback_status_size() - block_spacing();
    spinner_ = QPixmap(extent, extent);
    spinner_.fill(Qt::transparent);
    QPainter painter(&spinner_);
    painter.setRenderHint(QPainter::Antialiasing);
    Spinner::paint(palette().color(QPalette::Shadow), palette().color(QPalette::Base), painter, extent);
  }
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
    auto &block = blocks_.back();
    content_height_ -= block.bounding_rect(*this).height();
    block.events().emplace_back(&event);
    block.update_header(*this, state); // Updates timestamp if necessary
    content_height_ += block.bounding_rect(*this).height();
  } else {
    blocks_.emplace_back(*this, state, in, event);
    head_color_alternate_ = !head_color_alternate_;

    content_height_ += block_spacing();
    content_height_ += blocks_.back().bounding_rect(*this).height();
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

int TimelineView::block_margin() const { return fontMetrics().lineSpacing()/3; }
int TimelineView::block_spacing() const { return fontMetrics().lineSpacing() * 0.75; }
int TimelineView::avatar_size() const {
  auto metrics = fontMetrics();
  return metrics.height() * 2 + metrics.leading();
}
int TimelineView::scrollback_trigger_size() const {
  return viewport()->contentsRect().height()*2;
}
int TimelineView::scrollback_status_size() const {
  auto metrics = fontMetrics();
  return metrics.height() * 4;
}
int TimelineView::block_body_start() const {
  return avatar_size() + 2*block_margin();
}
int TimelineView::block_body_width() const {
  return visible_width() - (block_body_start() + block_margin());
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
  const float half_spacing = block_spacing()/2.0;
  bool alternate = head_color_alternate_;
  const int margin = block_margin();
  visible_blocks_.clear();
  SelectionScanner ss(selection_);
  for(auto it = blocks_.rbegin(); it != blocks_.rend(); ++it) {
    ss.advance(&*it);
    const auto bounds = it->bounding_rect(*this);
    offset.ry() -= bounds.height() + half_spacing;
    if((offset.y() + bounds.height() + half_spacing) < view_rect.top()) {
      // No further drawing possible
      break;
    }
    if(offset.y() - half_spacing < view_rect.bottom()) {
      visible_blocks_.push_back(VisibleBlock{&*it, bounds.translated(offset)});
      {
        const QRectF outline(offset.x(), offset.y() - half_spacing,
                             view_rect.width(), bounds.height() + block_spacing());
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(outline, margin*2, margin*2);
        painter.fillPath(path, palette().color(alternate ? QPalette::AlternateBase : QPalette::Base));
        painter.restore();
      }
      it->draw(*this, painter, offset, ss.fully_selected(&*it), ss.start_point(), ss.end_point());
    }
    offset.ry() -= half_spacing;
    alternate = !alternate;
  }
  if(backlog_growing_ && offset.y() > view_rect.top()) {
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const int extent = spinner_.width();
    painter.translate(view_rect.width() / 2, offset.y() - view_rect.top() - (extent/2 + half_spacing));
    auto t = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now());
    const qreal rotation_seconds = 2;
    const qreal angle = 360. * static_cast<qreal>(t.time_since_epoch().count() % static_cast<uint64_t>(1000 * rotation_seconds)) / (1000 * rotation_seconds);
    painter.rotate(angle);
    painter.drawPixmap(QPoint(-extent/2, -extent/2), spinner_);
    painter.restore();
    QTimer::singleShot(30, viewport(), static_cast<void (QWidget::*)()>(&QWidget::update));
  }
}

void TimelineView::resizeEvent(QResizeEvent *e) {
  verticalScrollBar()->setPageStep(viewport()->contentsRect().height() * 0.75);

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
    batch.events.emplace_front(*this, initial_state_, e);
    auto &internal = batch.events.front();
    if(!blocks_.empty()
       && blocks_.front().sender_id() == e.sender
       && internal.time - blocks_.front().events().front()->time <= BLOCK_MERGE_INTERVAL) {
      content_height_ -= blocks_.front().bounding_rect(*this).height();
      blocks_.front().events().emplace_front(&internal);
      blocks_.front().update_header(*this, initial_state_);  // Updates timestamp, disambig. display name, and avatar if necessary
      content_height_ += blocks_.front().bounding_rect(*this).height();
    } else {
      blocks_.emplace_front(*this, initial_state_, e, internal);
      content_height_ += blocks_.front().bounding_rect(*this).height() + block_spacing();
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
      const auto bounds = it->bounding_rect(*this);
      offset.ry() -= bounds.height();
      if((offset.y() + bounds.height()) < view_rect.top() - scrollback_trigger_size()) {
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
    if(!end_block) break;  // Batch overlaps with viewport, bail out

    int height_lost = 0;
    // Free blocks
    while(&blocks_.front() != end_block) {
      auto &block = blocks_.front();
      height_lost += block.bounding_rect(*this).height() + block_spacing();
      if(block.avatar()) unref_avatar(*block.avatar());
      pop_front_block();
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
      pop_front_block();
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

void TimelineView::mousePressEvent(QMouseEvent *event) {
  // TODO: Clickable files, images; context menus
  if(event->button() == Qt::LeftButton) {
    auto b = block_near(event->pos());
    auto rel_pos = event->pos() - b->bounds.topLeft();
    clicked_ = b->block->target_at(*this, rel_pos);
    if(!clicked_ || clicked_->type != ClickTarget::Type::EVENT) {
      QApplication::setOverrideCursor(Qt::IBeamCursor);
      if(b) {
        selection_ = Selection{b->block, rel_pos, b->block, rel_pos};
      } else {
        selection_ = {};
      }
      viewport()->update();
    }
  }
}

void TimelineView::mouseMoveEvent(QMouseEvent *event) {
  // TODO: Explore using hover event instead
  if(!clicked_ && event->buttons() & Qt::LeftButton) {
    // Update selection
    // TODO: Start auto-scrolling if cursor out of viewport
    if(auto b = block_near(event->pos())) {
      if(!selection_) {
        selection_ = Selection{};
        selection_->end = b->block;
        selection_->end_pos = event->pos() - b->bounds.topLeft();
      }
      selection_->end = b->block;
      selection_->end_pos = event->pos() - b->bounds.topLeft();
      QString t = selection_text();
      if(!t.isEmpty())
        QApplication::clipboard()->setText(selection_text(), QClipboard::Selection);

      viewport()->update();
    }
  } else {
    // Draw hover cursor
    if(auto b = block_near(event->pos())) {
      if(b->bounds.contains(event->pos())) {
        const auto rel_pos = event->pos() - b->bounds.topLeft();
        b->block->hover(*this, rel_pos);
      } else {
        viewport()->setCursor(Qt::ArrowCursor);
      }
    }
  }
}

void TimelineView::mouseReleaseEvent(QMouseEvent *event) {
  if(event->button() == Qt::LeftButton) {
    auto b = block_near(event->pos());
    if(b && clicked_ && clicked_ == b->block->target_at(*this, event->pos() - b->bounds.topLeft()) && clicked_->type != ClickTarget::Type::EVENT) {
      if(!QDesktopServices::openUrl(http_url(clicked_->url))) {
        qDebug() << "failed to open URL" << http_url(clicked_->url).toString(QUrl::FullyEncoded);
      }
    } else {
      QApplication::restoreOverrideCursor();
    }
  }
}

void TimelineView::copy() {
  QString t = selection_text();
  if(!t.isEmpty())
    QApplication::clipboard()->setText(t);
}

static float point_rect_dist(const QPointF &p, const QRectF &r) {
  auto cx = std::max(std::min(p.x(), r.right()), r.left());
  auto cy = std::max(std::min(p.y(), r.bottom()), r.top());
  auto dx = p.x() - cx;
  auto dy = p.y() - cy;
  return std::sqrt(dx*dx + dy*dy);
}

TimelineView::VisibleBlock *TimelineView::block_near(const QPoint &p) {
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
  return QSize(metrics.width('x')*80 + avatar_size() + verticalScrollBar()->sizeHint().width(), 2*metrics.lineSpacing());
}

QSize TimelineView::minimumSizeHint() const {
  auto metrics = fontMetrics();
  return QSize(metrics.width('x')*20 + avatar_size() + verticalScrollBar()->sizeHint().width(), 2*metrics.lineSpacing());
}

void TimelineView::focusOutEvent(QFocusEvent *e) {
  // Taken from QWidgetTextControl
  if(e->reason() != Qt::ActiveWindowFocusReason
     && e->reason() != Qt::PopupFocusReason) {
    selection_ = {};
    viewport()->update();
  }
}

void TimelineView::contextMenuEvent(QContextMenuEvent *event) {
  if(auto b = block_near(event->pos())) {
    const auto rel_pos = event->pos() - b->bounds.topLeft();
    if(auto target = b->block->target_at(*this, rel_pos)) {
      menu_->clear();
      if(target->type != ClickTarget::Type::EVENT) {
        menu_->addSection("Link");
        const QUrl &url = target->url;
        if(url.scheme() == "mxc") {
          auto http_action = menu_->addAction(QIcon::fromTheme("edit-copy"), tr("&Copy link HTTP address"));
          connect(http_action, &QAction::triggered, [this, url]() {
              auto data = new QMimeData;
              auto hurl = http_url(url);
              data->setText(hurl.toString(QUrl::FullyEncoded));
              data->setUrls({hurl});
              QApplication::clipboard()->setMimeData(data);
            });
        }
        auto copy_action = menu_->addAction(QIcon::fromTheme("edit-copy"), url.scheme() == "mxc" ? tr("Copy link &MXC address") : tr("&Copy link address"));
        connect(copy_action, &QAction::triggered, [url]() {
            auto data = new QMimeData;
            data->setText(url.toString(QUrl::FullyEncoded));
            data->setUrls({url});
            QApplication::clipboard()->setMimeData(data);
          });
      }
      if(target->event) {
        menu_->addSection("Event");
        const QString &id = target->event->data.event_id;
        auto redact_action = menu_->addAction(QIcon::fromTheme("edit-delete"), tr("&Redact..."));
        connect(redact_action, &QAction::triggered, [this, id]() {
            auto dialog = new RedactDialog(this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            connect(dialog, &QDialog::accepted, [this, id, dialog]() {
                room_.redact(id, dialog->reason());
              });
            dialog->open();
          });
      }
      menu_->popup(event->globalPos());
    }
  }
}

QUrl TimelineView::http_url(const QUrl &url) const {
  if(url.scheme() == "mxc") {
    return matrix::Content(url).url_on(room_.session().homeserver());
  }
  return url;
}
