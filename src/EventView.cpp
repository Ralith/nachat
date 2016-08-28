#include "EventView.hpp"

#include <sstream>
#include <iomanip>
#include <ctime>
#include <cmath>

#include <QMenu>
#include <QDebug>
#include <QPainter>
#include <QToolTip>
#include <QClipboard>
#include <QApplication>
#include <QDesktopServices>
#include <QMessageBox>
#include <QRegularExpression>
#include <QMimeData>
#include <QPointer>

#include "qstringbuilder.h"

#include "matrix/Session.hpp"

#include "RedactDialog.hpp"
#include "EventSourceView.hpp"

using std::experimental::optional;

constexpr static std::chrono::minutes TIMESTAMP_RANGE_THRESHOLD(2);

static auto to_time_point(uint64_t ts) {
  return std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::from_time_t(0))
      + std::chrono::duration<uint64_t, std::milli>(ts);
}

static QString pretty_size(double n) {
  constexpr const static char *const units[9] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB"}; // should be enough for anyone!
  auto idx = std::min<size_t>(8, std::log(n)/std::log(1024.));
  return QString::number(n / std::pow<double>(1024, idx), 'g', 4) + " " + units[idx];
}

std::vector<std::pair<QString, QVector<QTextLayout::FormatRange>>>
  BlockRenderInfo::format_text(const matrix::RoomState &state, const matrix::event::Room &evt, const QString &str) const {

  std::vector<std::pair<QString, QVector<QTextLayout::FormatRange>>> result;
  const static QRegularExpression line_re("\\R", QRegularExpression::UseUnicodePropertiesOption | QRegularExpression::OptimizeOnFirstUsageOption);
  auto lines = str.split(line_re);
  result.reserve(lines.size());
  std::transform(lines.begin(), lines.end(), std::back_inserter(result),
                 [](const QString &s){ return std::pair<QString, QVector<QTextLayout::FormatRange>>(s, {}); });

  if(evt.type() == matrix::event::room::Message::tag()) {
    matrix::event::room::Message msg(evt);
    if(msg.content().type() == matrix::event::room::message::Emote::tag() && !result.empty()) {
      auto &member = *state.member_from_id(evt.sender());
      result.front().first = "* " % member.pretty_name() % " " % result.front().first;
    }
  }

  // Detect highlights
  auto self_member = state.member_from_id(self());
  const bool highlight =
    self_member
    && ((self_member->displayname() && str.toCaseFolded().contains(self_member->displayname()->toCaseFolded()))
        || str.toCaseFolded().contains(self().value()));
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
    R"(\b()"
    R"([a-z][a-z0-9+-.]*://[^\s]+)"
    R"(|[^\s]+\.(com|net|org)(/[^\s]*)?)"
    R"(|www\.[^\s]+\.[^\s]+)"
    R"(|data:[^\s]+)"
    R"())",
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

Event::Event(const BlockRenderInfo &info, const matrix::RoomState &state, const matrix::event::Room &e)
  : data(e), time(to_time_point(e.origin_server_ts())) {
  std::vector<std::pair<QString, QVector<QTextLayout::FormatRange>>> lines;
  if(e.type() == matrix::event::room::Message::tag()) {
    matrix::event::room::Message msg(e);
    const auto &content = msg.content();
    if(content.type() == matrix::event::room::message::File::tag()
       || content.type() == matrix::event::room::message::Image::tag()
       || content.type() == matrix::event::room::message::Video::tag()
       || content.type() == matrix::event::room::message::Audio::tag()) {
      matrix::event::room::message::FileLike file(content);
      lines.emplace_back();
      auto &line = lines.front();
      if(content.type() == matrix::event::room::message::File::tag() && content.body() == "") {
        line.first = matrix::event::room::message::File(file).filename();
      } else {
        line.first = file.body();
      }
      line.first = content.body();
      {
        QTextLayout::FormatRange range;
        range.start = 0;
        range.length = line.first.length();
        QTextCharFormat format;
        format.setAnchor(true);
        format.setAnchorHref(file.url());
        format.setForeground(info.palette().link());
        format.setFontUnderline(true);
        range.format = format;
        line.second.push_back(range);
      }
      auto type = file.mimetype();
      auto size = file.size();
      if(type || size)
        line.first += " (";
      if(size)
        line.first += pretty_size(*size);
      if(type) {
        if(size) line.first += " ";
        line.first += *type;
      }
      if(type || size)
        line.first += ")";
    } else {
      lines = info.format_text(state, e, content.body());
    }
  } else if(e.type() == matrix::event::room::Member::tag()) {
    matrix::event::room::Member member{matrix::event::room::State{e}};
    switch(member.content().membership()) {
    case matrix::Membership::INVITE: {
      auto invitee = state.member_from_id(member.user());
      if(!invitee) {
        qDebug() << "got invite for non-member" << member.user().value() << "probably due to SYN-645";
        lines = info.format_text(state, e, QObject::tr("SYN-645 related error"));
      } else {
        lines = info.format_text(state, e, QObject::tr("invited %1").arg(state.member_name(*invitee)));
      }
      break;
    }
    case matrix::Membership::JOIN: {
      if(!member.prev_content()) {
        lines = info.format_text(state, e, QObject::tr("joined"));
        break;
      }
      const auto &prev = *member.prev_content();
      switch(prev.membership()) {
      case matrix::Membership::INVITE:
        lines = info.format_text(state, e, QObject::tr("accepted invite"));
        break;
      case matrix::Membership::JOIN: {
        const bool avatar_changed = prev.avatar_url() != member.content().avatar_url();
        const auto new_dn = member.content().displayname();
        const auto old_dn = prev.displayname();
        const bool dn_changed = old_dn != new_dn;
        if(avatar_changed && dn_changed) {
          if(!new_dn)
            lines = info.format_text(state, e, QObject::tr("unset display name and changed avatar"));
          else
            lines = info.format_text(state, e, QObject::tr("changed display name to %1 and changed avatar").arg(*new_dn));
        } else if(avatar_changed) {
          lines = info.format_text(state, e, QObject::tr("changed avatar"));
        } else if(dn_changed) {
          if(!new_dn) {
            lines = info.format_text(state, e, QObject::tr("unset display name"));
          } else if(!old_dn) {
            lines = info.format_text(state, e, QObject::tr("set display name to %1").arg(*new_dn));
          } else {
            lines = info.format_text(state, e, QObject::tr("changed display name from %1 to %2").arg(*old_dn).arg(*new_dn));
          }
        } else {
          lines = info.format_text(state, e, QObject::tr("sent a no-op join"));
        }
        break;
      }
      default:
        lines = info.format_text(state, e, QObject::tr("joined"));
        break;
      }
      break;
    }
    case matrix::Membership::LEAVE: {
      if(member.user() == member.sender()) {
        lines = info.format_text(state, e, QObject::tr("left"));
      } else {
        auto gone = state.member_from_id(member.user());
        if(!gone) {
          qDebug() << "got leave for non-member" << member.user().value();
        }
        lines = info.format_text(state, e, QObject::tr("kicked %1").arg(gone ? state.member_name(*gone) : member.user().value()));
      }
      break;
    }
    case matrix::Membership::BAN: {
      auto banned = state.member_from_id(member.user());
      if(!banned) {
        qDebug() << "INTERNAL ERROR: displaying ban of unknown member" << member.user().value();
        lines = info.format_text(state, e, QObject::tr("banned %1").arg(member.user().value()));
      } else {
        lines = info.format_text(state, e, QObject::tr("banned %1").arg(state.member_name(*banned)));
      }
      break;
    }
    }
  } else if(e.type() == matrix::event::room::Create::tag()) {
    lines = info.format_text(state, e, QObject::tr("created the room"));
  } else {
    lines = info.format_text(state, e, QObject::tr("unrecognized event type %1").arg(e.type().value()));
  }
  layouts = std::vector<QTextLayout>(lines.size());
  QTextOption body_options;
  body_options.setAlignment(Qt::AlignLeft | Qt::AlignTop);
  body_options.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  for(size_t i = 0; i < lines.size(); ++i) {
    layouts[i].setFont(info.font());
    layouts[i].setTextOption(body_options);
    layouts[i].setCacheEnabled(true);
    layouts[i].setText(lines[i].first);
    layouts[i].setFormats(lines[i].second);
  }

  update_layout(info);
}

static QString to_timestamp(const char *format, std::chrono::system_clock::time_point p) {
  auto time = std::chrono::system_clock::to_time_t(p);
  auto tm = std::localtime(&time);
  std::ostringstream s;
  s << std::put_time(tm, format);
  return QString::fromStdString(s.str());
}

Block::Block(const BlockRenderInfo &info, const matrix::RoomState &state, Event &e)
  : sender_id_(e.data.sender()) {
  events_.emplace_back(&e);

  {
    QTextOption options;
    options.setAlignment(Qt::AlignLeft | Qt::AlignTop);
    options.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    name_layout_.setFont(info.font());
    name_layout_.setTextOption(options);
    name_layout_.setCacheEnabled(true);
  }

  {
    QTextOption options;
    options.setAlignment(Qt::AlignRight | Qt::AlignTop);
    options.setWrapMode(QTextOption::NoWrap);
    timestamp_layout_.setFont(info.font());
    timestamp_layout_.setTextOption(options);
    timestamp_layout_.setCacheEnabled(true);
  }

  update_header(info, state);
}

static matrix::event::room::MemberContent get_header_content(const matrix::Member &m, const matrix::event::Room &e) {
  if(e.type() != matrix::event::room::Member::tag()) return m.content();
  matrix::event::room::Member me{matrix::event::room::State{e}};
  if(me.sender() != me.user()) return m.content();
  if(!me.prev_content() || me.prev_content()->membership() == matrix::Membership::LEAVE) return me.content();
  return *me.prev_content();
}

void Block::update_header(const BlockRenderInfo &info, const matrix::RoomState &state) {
  auto sender = state.member_from_id(sender_id_);
  if(sender) {
    const auto &header_content = get_header_content(*sender, events().back()->data);
    if(header_content.avatar_url()) {
      try {
        avatar_ = matrix::Content(*header_content.avatar_url());
      } catch(const std::invalid_argument &e) {
        qDebug() << sender_id().value() << "has invalid avatar URL:" << e.what() << ":" << *header_content.avatar_url();
        avatar_ = {};
      }
    } else {
      avatar_ = {};
    }

    const auto &name = header_content.displayname() ? *header_content.displayname() : sender_id_.value();
    auto disambig = state.member_disambiguation(*sender); // FIXME: Out of sync with a just-changed display name
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
    if(events_.front()->data.type() != matrix::event::room::Create::tag())
      qDebug() << "block sender" << sender_id().value() << "is not a member probably due to SYN-645";
    name_layout_.setText(sender_id_.value());
  }

  update_layout(info);
}

void Block::update_layout(const BlockRenderInfo &info) {
  auto metrics = info.metrics();

  {
    qreal height = 0;
    name_layout_.beginLayout();
    while(true) {
      auto line = name_layout_.createLine();
      if(!line.isValid()) break;
      line.setLineWidth(info.body_width());
      line.setPosition(QPointF(info.body_start(), height));
      height += metrics.lineSpacing();
    }
    name_layout_.endLayout();
  }

  {  // Lay out as a single or range timestamp as appropriate, degrading to single or nothing of space is unavailable
    auto layout_ts = [&](){
      timestamp_layout_.beginLayout();
      auto line = timestamp_layout_.createLine();
      line.setLineWidth(info.body_width());
      line.setPosition(QPointF(info.body_start(), 0));
      timestamp_layout_.endLayout();
      if(name_layout_.lineAt(0).naturalTextWidth() > info.body_width() - line.naturalTextWidth()) {
        timestamp_layout_.clearLayout();
        return false;
      }
      return true;
    };
    auto start_ts = to_timestamp("%H:%M", events_.front()->time);
    bool done = false;
    if(events_.size() > 1 && events_.back()->time - events_.front()->time > TIMESTAMP_RANGE_THRESHOLD) {
      auto end_ts = to_timestamp("%H:%M", events_.back()->time);
      timestamp_layout_.setText(start_ts % "–" % end_ts);
      done = layout_ts();
    }
    if(!done) {
      timestamp_layout_.setText(start_ts);
      layout_ts();
    }
  }
}

void Event::update_layout(const BlockRenderInfo &g) {
  qreal height = 0;
  for(auto &layout : layouts) {
    layout.beginLayout();
    while(true) {
      auto line = layout.createLine();
      if(!line.isValid()) break;
      line.setLineWidth(g.body_width());
      line.setPosition(QPointF(g.body_start(), height));
      height += g.metrics().lineSpacing();
    }
    layout.endLayout();
  }
}

QRectF Event::bounding_rect() const {
  QRectF rect;
  for(const auto &layout : layouts) {
    rect |= layout.boundingRect();
  }
  return rect;
}

struct Cursor {
  QTextLine line;
  int cursor;
};

static optional<Cursor> cursor_at(const QTextLayout &layout, const QPointF &p) {
  for(int i = 0; i < layout.lineCount(); ++i) {
    const auto line = layout.lineAt(i);
    if(line.naturalTextRect().contains(p)) {
      return optional<Cursor>({line, line.xToCursor(p.x())});
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

static void open_url(const matrix::Session &session, const QUrl &url) {
  if(!QDesktopServices::openUrl(session.ensure_http(url))) {
    qDebug() << "failed to open URL" << session.ensure_http(url).toString(QUrl::FullyEncoded);
  }
}

optional<QUrl> Event::link_at(const QPointF &point) const {
  for(const auto &layout : layouts) {
    if(auto cursor = cursor_at(layout, point)) {
      if(auto link = link_at(layout, cursor->cursor)) {
        return link;
      }
      break;
    }
  }
  return {};
}

optional<QUrl> Event::link_at(const QTextLayout &layout, int cursor) const {
  auto formats = formats_at(layout, cursor);
  auto anchor = std::find_if(formats.begin(), formats.end(),
                             [](const QTextLayout::FormatRange &f) { return f.format.isAnchor(); });
  if(anchor != formats.end()) {
    return QUrl{anchor->format.anchorHref(), QUrl::StrictMode};
  }
  return {};
}

void Event::event(matrix::Room &room, QWidget &container, const optional<QPointF> &pos, QEvent *e) {
  e->setAccepted(false);

  if(!pos) return;
  optional<QUrl> target;
  optional<QRectF> line_bounds;
  for(const auto &layout : layouts) {
    if(auto cursor = cursor_at(layout, *pos)) {
      target = link_at(layout, cursor->cursor);
      line_bounds = cursor->line.naturalTextRect();
      break;
    }
  }

  e->setAccepted(true);
  

  switch(e->type()) {
  case QEvent::MouseMove:
    if(target) {
      container.setCursor(Qt::PointingHandCursor);
    } else if(line_bounds && pos && line_bounds->contains(*pos)) {
      container.setCursor(Qt::IBeamCursor);
    } else {
      container.setCursor(Qt::ArrowCursor);
    }
    break;
  case QEvent::MouseButtonRelease:
    if(static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton && target) {
      open_url(room.session(), *target);
    }
    break;
  case QEvent::ToolTip:
    if(pos && line_bounds && line_bounds->contains(*pos)) {
      QToolTip::showText(static_cast<QHelpEvent*>(e)->globalPos(), to_timestamp("%H:%M:%S", time));
    } else {
      QToolTip::hideText();
      e->ignore();
    }
    break;
  default:
    e->setAccepted(false);
    break;
  }
}

static void populate_menu_link(const matrix::Room &room, QMenu &menu, const QUrl &url) {
  menu.addSection(QObject::tr("Link"));
  if(url.scheme() == "mxc") {
    auto http_action = menu.addAction(QIcon::fromTheme("edit-copy"), QObject::tr("&Copy link HTTP address"));
    auto hurl = room.session().ensure_http(url);
    QObject::connect(http_action, &QAction::triggered, [=]() {
        auto data = new QMimeData;
        data->setText(hurl.toString(QUrl::FullyEncoded));
        data->setUrls({hurl});
        QApplication::clipboard()->setMimeData(data);

        auto data2 = new QMimeData;
        data2->setText(hurl.toString(QUrl::FullyEncoded));
        data2->setUrls({hurl});
        QApplication::clipboard()->setMimeData(data2, QClipboard::Selection);
      });
  }
  auto copy_action = menu.addAction(QIcon::fromTheme("edit-copy"), url.scheme() == "mxc" ? QObject::tr("Copy link &MXC address") : QObject::tr("&Copy link address"));
  QObject::connect(copy_action, &QAction::triggered, [=]() {
      auto data = new QMimeData;
      data->setText(url.toString(QUrl::FullyEncoded));
      data->setUrls({url});
      QApplication::clipboard()->setMimeData(data);
      auto data2 = new QMimeData;
      data2->setText(url.toString(QUrl::FullyEncoded));
      data2->setUrls({url});
      QApplication::clipboard()->setMimeData(data2, QClipboard::Selection);
    });
}

void Event::populate_menu(matrix::Room &room, QMenu &menu, const QPointF &pos) const {
  auto target = link_at(pos);
  if(target) {
    populate_menu_link(room, menu, *target);
  }
  menu.addSection(QObject::tr("Event"));
  const matrix::EventID &event_id = data.id();
  const matrix::RoomID &room_id = room.id();
  QPointer<matrix::Session> session = &room.session();

  auto redact_action = menu.addAction(QIcon::fromTheme("edit-delete"), QObject::tr("&Redact..."));
  QObject::connect(redact_action, &QAction::triggered, [session, room_id, event_id]() {
      auto dialog = new RedactDialog;
      dialog->setAttribute(Qt::WA_DeleteOnClose);
      QObject::connect(dialog, &QDialog::accepted, [session, room_id, event_id, dialog]() {
          if(!session) return;
          auto r = session->room_from_id(room_id);
          if(r) {
            r->redact(event_id, dialog->reason());
          } else {
            qDebug() << "couldn't redact" << event_id.value() << "without apparently being in room" << room_id.value();
          }
        });
      dialog->open();
    });

  auto source_action = menu.addAction(QObject::tr("&View source..."));
  QJsonObject source = data.json();
  QObject::connect(source_action, &QAction::triggered, [source]() {
      (new EventSourceView(source))->show();
    });
}

QRectF Block::bounding_rect(const BlockRenderInfo &info) const {
  const auto metrics = info.metrics();
  QRectF rect(0, 0, info.width() - 2*info.margin(), info.avatar_size());
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

void Block::draw(const BlockRenderInfo &info, QPainter &p, QPointF offset, const QPixmap &avatar_pixmap,
                               bool focused, bool select_all, optional<QPointF> select_start, optional<QPointF> select_end) const {
  auto metrics = info.metrics();

  {
    const auto av_size = info.avatar_size();
    const QSize logical_size = avatar_pixmap.size() / avatar_pixmap.devicePixelRatio();
    p.drawPixmap(QPointF(offset.x() + info.margin() + (av_size - logical_size.width()) / 2.,
                         offset.y() + (av_size - logical_size.height()) / 2.),
                 avatar_pixmap);
  }

  QVector<QTextLayout::FormatRange> selections;
  selections.push_back(QTextLayout::FormatRange());
  const auto cg = focused ? QPalette::Active : QPalette::Inactive;
  selections.front().format.setBackground(info.palette().brush(cg, QPalette::Highlight));
  selections.front().format.setForeground(info.palette().brush(cg, QPalette::HighlightedText));
  auto &&sel = [&](const QTextLayout &l, QPointF o) {
    auto range = selection_for(l, o, select_all, select_start, select_end);
    selections.front().start = range.start;
    selections.front().length = range.length;
  };

  p.save();
  p.setPen(info.palette().color(QPalette::Dark));
  sel(name_layout_, QPointF(0, 0));
  name_layout_.draw(&p, offset, selections);
  sel(timestamp_layout_, QPointF(0, 0));
  timestamp_layout_.draw(&p, offset, selections);
  p.restore();

  QPointF local_offset(0, name_layout_.boundingRect().height() + info.event_spacing());

  for(const auto event : events_) {
    p.save();
    if(event->data.type() != matrix::event::room::Message::tag()) {
      p.setPen(info.palette().color(QPalette::Dark));
    }
    QRectF event_bounds;
    for(const auto &layout : event->layouts) {
      sel(layout, local_offset);
      layout.draw(&p, offset + local_offset, selections);
      event_bounds |= layout.boundingRect();
    }
    local_offset.ry() += event_bounds.height() + info.event_spacing();
    p.restore();
  }
}

QString Block::selection_text(const QFontMetrics &metrics, bool select_all,
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

size_t Block::size() const {
  return events_.size();
}

EventHit Block::event_at(const QFontMetrics &metrics, const QPointF &p) {
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

void Block::event(matrix::Room &room, QWidget &container, const BlockRenderInfo &info, const optional<QPointF> &pos, QEvent *e) {
  EventHit event_hit;
  if(pos) {
    event_hit = event_at(info.metrics(), *pos);
    if(event_hit) {
      event_hit.event->event(room, container, event_hit.pos, e);
      if(e->isAccepted()) return;
    }
  }

  const QRectF avatar_rect(info.margin(), 0, info.avatar_size(), info.avatar_size());
  const QRectF &header_rect = name_layout_.boundingRect();

  const QRectF ts_rect = timestamp_layout_.lineCount() == 0 ? QRectF()
    : QRectF(timestamp_layout_.boundingRect().right() - timestamp_layout_.lineAt(0).naturalTextWidth(),
             timestamp_layout_.boundingRect().top(),
             timestamp_layout_.lineAt(0).naturalTextWidth(),
             timestamp_layout_.boundingRect().height());

  switch(e->type()) {
  case QEvent::MouseMove: {
    if(pos && avatar_ && avatar_rect.contains(*pos)) {
      container.setCursor(Qt::PointingHandCursor);
    } else if(pos && header_rect.contains(*pos)) {
      container.setCursor(Qt::IBeamCursor);
    } else {
      container.setCursor(Qt::ArrowCursor);
    }
    break;
  }
  case QEvent::MouseButtonPress:
    break;
  case QEvent::MouseButtonRelease:
    if(static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton && pos && avatar_ && avatar_rect.contains(*pos)) {
      open_url(room.session(), avatar_->url());
    }
    break;
  case QEvent::ContextMenu: {
    auto menu = new QMenu;
    menu->setAttribute(Qt::WA_DeleteOnClose);
    if(event_hit) {
      event_hit.event->populate_menu(room, *menu, event_hit.pos);
    }
    if(pos && avatar_ && avatar_rect.contains(*pos)) {
      populate_menu_link(room, *menu, avatar_->url());
    }
    menu->addSection(QObject::tr("User"));
    auto profile_action = menu->addAction(QIcon::fromTheme("user-available"), QObject::tr("View &profile..."));
    QObject::connect(profile_action, &QAction::triggered, [&room, this]() {
        auto member = room.state().member_from_id(sender_id());
        // TODO: Store this info with the block so it's available even if the user left
        auto dialog = new QMessageBox(QMessageBox::Information,
                                      QObject::tr("Profile of %1")
                                      .arg(member ? room.state().member_name(*member) : sender_id().value()),
                                      QObject::tr("MXID: %1\nDisplay name: %2")
                                      .arg(sender_id().value())
                                      .arg(member && member->displayname() ? *member->displayname() : ""),
                                      QMessageBox::Close);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->open();
      });

    menu->popup(static_cast<QContextMenuEvent*>(e)->globalPos());
    break;
  }
  case QEvent::ToolTip:
    if(pos && ts_rect.contains(*pos)) {
      QToolTip::showText(static_cast<QHelpEvent*>(e)->globalPos(), to_timestamp("%Y-%m-%d", events().front()->time));
    } else if(pos && (avatar_rect.contains(*pos) || header_rect.contains(*pos))) {
      QToolTip::showText(static_cast<QHelpEvent*>(e)->globalPos(), sender_id().value());
    } else {
      QToolTip::hideText();
      e->ignore();
    }
    break;
  default:
    break;
  }
}
