#include "Room.hpp"

#include <unordered_set>
#include <memory>
#include <algorithm>

#include <QtNetwork>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDebug>

#include "proto.hpp"
#include "Session.hpp"
#include "utils.hpp"

using std::experimental::optional;

namespace matrix {

RoomState::RoomState(const QJsonObject &info, lmdb::txn &txn, lmdb::dbi &member_db) {
  if(info["name"].isString()) {
    name_ = info["name"].toString();
  }
  if(info["canonical_alias"].isString()) {
    canonical_alias_ = info["canonical_alias"].toString();
  }
  if(info["topic"].isString()) {
    topic_ = info["topic"].toString();
  }
  if(info["avatar"].isString()) {
    avatar_ = info["avatar"].toString();
  }

  const auto &as = info["aliases"].toArray();
  aliases_.reserve(as.size());
  std::transform(as.begin(), as.end(), std::back_inserter(aliases_),
                 [](const QJsonValue &v) {
                   return v.toString();
                 });

  lmdb::val id_val;
  lmdb::val state;
  auto cursor = lmdb::cursor::open(txn, member_db);
  while(cursor.get(id_val, state, MDB_NEXT)) {
    const auto id_str = UserID(QString::fromUtf8(id_val.data(), id_val.size()));
    event::room::MemberContent content(event::Content(QJsonDocument::fromBinaryData(QByteArray(state.data(), state.size())).object()));
    auto &member = members_by_id_.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(id_str),
      std::forward_as_tuple(id_str, content)).first->second;
    if(member.displayname())
      record_displayname(member.id(), *member.displayname(), nullptr);
  }
}

QJsonObject RoomState::to_json() const {
  QJsonObject o;
  if(name_) o["name"] = *name_;
  if(canonical_alias_) o["canonical_alias"] = *canonical_alias_;
  if(topic_) o["topic"] = *topic_;
  if(!avatar_.isEmpty()) o["avatar"] = avatar_.toString(QUrl::FullyEncoded);

  QJsonArray aa;
  for(const auto &x : aliases_) {
    aa.push_back(x);
  }
  o["aliases"] = std::move(aa);

  return o;
}

QString RoomState::pretty_name(const UserID &own_id) const {
  if(name_ && !name_->isEmpty()) return *name_;
  if(canonical_alias_) return *canonical_alias_;
  if(!aliases_.empty()) return aliases_[0];  // Non-standard, but matches vector-web
  // FIXME: Maintain earliest two IDs as state!
  auto ms = members();
  ms.erase(std::remove_if(ms.begin(), ms.end(), [&](const Member *m){ return m->id() == own_id; }), ms.end());
  if(ms.size() > 1) {
    std::partial_sort(ms.begin(), ms.begin() + 2, ms.end(),
                      [](const Member *a, const Member *b) {
                        return a->id() < b->id();
                      });
  }
  switch(ms.size()) {
  case 0: return QObject::tr("Empty room");
  case 1: return ms[0]->pretty_name();
  case 2: return QObject::tr("%1 and %2").arg(member_name(*ms[0])).arg(member_name(*ms[1]));
  default: return QObject::tr("%1 and %2 others").arg(member_name(*ms[0])).arg(ms.size() - 1);
  }
}

QString RoomState::member_disambiguation(const Member &member) const {
  if(!member.displayname()) {
    if(members_by_displayname_.find(member.id().value()) != members_by_displayname_.end()) {
      return member.id().value();
    } else {
      return "";
    }
  } else if(members_named(*member.displayname()).size() > 1
            || (member.displayname() && member_from_id(UserID(member.displayname()->normalized(QString::NormalizationForm_C))))) {
    return member.id().value();
  } else {
    return "";
  }
}

QString RoomState::member_name(const Member &member) const {
  auto result = member.pretty_name();
  auto disambig = member_disambiguation(member);
  if(disambig.isEmpty()) return result;
  return result % " (" % disambig % ")";
}

std::vector<UserID> &RoomState::members_named(QString displayname) {
  return members_by_displayname_.at(displayname.normalized(QString::NormalizationForm_C));
}
const std::vector<UserID> &RoomState::members_named(QString displayname) const {
  return members_by_displayname_.at(displayname.normalized(QString::NormalizationForm_C));
}

std::vector<const Member *> RoomState::members() const {
  std::vector<const Member *> result;
  result.reserve(members_by_id_.size());
  std::transform(members_by_id_.begin(), members_by_id_.end(), std::back_inserter(result),
                 [](auto &x) { return &x.second; });
  return result;
}

void RoomState::forget_displayname(const UserID &id, const QString &old_name_in, Room *room) {
  const QString old_name = old_name_in.normalized(QString::NormalizationForm_C);
  auto &vec = members_by_displayname_.at(old_name);
  QString other_disambiguation;
  const Member *other_member = nullptr;
  const bool existing_displayname = vec.size() == 2;
  const Member *const existing_mxid = member_from_id(UserID(old_name));
  if(room && (!existing_displayname || !existing_mxid)) {
    if(existing_displayname) other_member = &members_by_id_.at(vec[0] == id ? vec[1] : vec[0]);
    if(existing_mxid) other_member = existing_mxid;
  }
  if(other_member) {
    other_disambiguation = member_disambiguation(*other_member);
  }
  const auto before = vec.size();
  vec.erase(std::remove(vec.begin(), vec.end(), id), vec.end());
  assert(before - vec.size() == 1);
  if(vec.empty()) {
    members_by_displayname_.erase(old_name);
  }
  if(other_member) {
    room->member_disambiguation_changed(*other_member, other_disambiguation);
  }
}

void RoomState::record_displayname(const UserID &id, const QString &name, Room *room) {
  const auto normalized = name.normalized(QString::NormalizationForm_C);
  auto &vec = members_by_displayname_[normalized];
  for(const auto &x : vec) {
    assert(x != id);
  }
  vec.push_back(id);

  if(room) {
    const Member *other_member = nullptr;
    const bool existing_displayname = vec.size() == 2;
    const Member *const existing_mxid = member_from_id(UserID(normalized));
    if(!existing_displayname || !existing_mxid) {
      // If there's only one user with the name, they get newly disambiguated too
      if(existing_displayname) other_member = &members_by_id_.at(vec[0]);
      if(existing_mxid) other_member = existing_mxid;
    }
    if(other_member) room->member_disambiguation_changed(*other_member, "");
  }
}

const Member *RoomState::member_from_id(const UserID &id) const {
  auto it = members_by_id_.find(id);
  if(it == members_by_id_.end()) return nullptr;
  return &it->second;
}

static constexpr std::chrono::steady_clock::duration MINIMUM_BACKOFF(std::chrono::seconds(5));
// Default synapse seconds-per-message when throttled

Room::Room(Matrix &universe, Session &session, RoomID id, const QJsonObject &initial,
           lmdb::env &env, lmdb::txn &txn, lmdb::dbi &&member_db)
    : universe_(universe), session_(session), id_(std::move(id)),
      db_env_(env), member_db_(std::move(member_db)), transmitting_(nullptr), retry_backoff_(MINIMUM_BACKOFF)
{
  transmit_retry_timer_.setSingleShot(true);
  connect(&transmit_retry_timer_, &QTimer::timeout, this, &Room::transmit_event);

  if(!initial.isEmpty()) {
    initial_state_ = RoomState(initial["initial_state"].toObject(), txn, member_db_);
    state_ = initial_state_;

    QJsonObject b = initial["buffer"].toObject();
    if(!b.isEmpty()) {
      buffer_.emplace_back();
      buffer_.back().prev_batch = b["prev_batch"].toString();
      buffer_.back().events.reserve(b.size());
      auto es = b["events"].toArray();
      for(const auto &e : es) {
        event::Room evt(event::Identifiable(Event(e.toObject())));
        buffer_.back().events.emplace_back(evt);
        if(auto s = evt.to_state()) state_.apply(*s);
        state_.prune_departed();
      }
    }
    highlight_count_ = initial["highlight_count"].toDouble(0);
    notification_count_ = initial["notification_count"].toDouble(0);
    auto receipts = initial["receipts"].toObject();
    for(auto it = receipts.begin(); it != receipts.end(); ++it) {
      update_receipt(UserID(it.key()), EventID(it.value().toObject()["event_id"].toString()), it.value().toObject()["ts"].toDouble());
    }
  }
}

QString Room::pretty_name() const {
  return state_.pretty_name(session_.user_id());
}

void Room::load_state(lmdb::txn &txn, gsl::span<const event::room::State> events) {
  for(auto &state : events) {
    try {
      initial_state_.apply(state);
      initial_state_.prune_departed();
      state_.dispatch(state, this, &member_db_, &txn);
      state_.prune_departed();
    } catch(malformed_event &e) {
      qDebug() << "WARNING:" << id().value() << "ignoring malformed state:" << e.what();
      qDebug() << state.json();
    }
  }
}

size_t Room::buffer_size() const {
  size_t r = 0;
  for(auto &x : buffer_) { r += x.events.size(); }
  return r;
}

QJsonObject Room::to_json() const {
  QJsonObject o;
  if(!buffer_.empty()) {
    o["prev_batch"] = buffer_.back().prev_batch;
    QJsonArray es;
    for(const auto &x : buffer_.back().events) {
      es.push_back(x.json());
    }
    o["events"] = std::move(es);
  }
  QJsonObject receipts;
  for(const auto &receipt : receipts_by_user_) {
    receipts[receipt.first.value()] = QJsonObject{{"event_id", receipt.second.event.value()}, {"ts", static_cast<qint64>(receipt.second.ts)}};
  }
  return QJsonObject{
    {"initial_state", initial_state_.to_json()},
    {"buffer", std::move(o)},
    {"highlight_count", static_cast<double>(highlight_count_)},
    {"notification_count", static_cast<double>(notification_count_)},
    {"receipts", receipts}
  };
}

bool Room::dispatch(lmdb::txn &txn, const proto::JoinedRoom &joined) {
  bool state_touched = false;

  if(joined.unread_notifications.highlight_count != highlight_count_) {
    auto old = highlight_count_;
    highlight_count_ = joined.unread_notifications.highlight_count;
    highlight_count_changed(old);
  }

  if(joined.unread_notifications.notification_count != notification_count_) {
    auto old = notification_count_;
    notification_count_ = joined.unread_notifications.notification_count;
    notification_count_changed(old);
  }

  if(joined.timeline.limited) {
    buffer_.clear();
    discontinuity();
  }

  prev_batch(joined.timeline.prev_batch);
  // Must be called *after* discontinuity so that users can easily discard existing timeline events

  // Ensure that only the first batch in the buffer can ever be empty
  if(joined.timeline.events.empty() && !buffer_.empty()) {
    buffer_.back().prev_batch = joined.timeline.prev_batch;
  } else {
    buffer_.emplace_back();     // In-place so has_unread is always up to date
    auto &batch = buffer_.back();
    batch.prev_batch = joined.timeline.prev_batch;
    batch.events.reserve(joined.timeline.events.size());
    for(auto &evt : joined.timeline.events) {
      if(auto s = evt.to_state()) {
        try {
          state_touched |= state_.dispatch(*s, this, &member_db_, &txn);
        } catch(const malformed_event &e) {
          qDebug() << "WARNING:" << id().value() << "ignoring malformed state:" << e.what();
          qDebug() << s->json();
        }
      }

      // Must be placed before `message` so resulting calls to `has_unread` return accurate results accounting for the
      // message in question
      batch.events.emplace_back(evt);

      message(evt);

      // Must happen after we dispatch the previous event but before we process the next one, to ensure display names are
      // correct for leave/ban events as well as whatever follows
      state_.prune_departed(this);
    }

    while(!buffer_.empty() && (buffer_size() - buffer_.front().events.size()) >= session_.buffer_size()) {
      for(auto &evt : buffer_.front().events) {
        if(auto s = evt.to_state())
          initial_state_.apply(*s);
        initial_state_.prune_departed();
      }
      buffer_.pop_front();
    }
  }

  for(const auto &evt : joined.ephemeral.events) {
    if(evt.type() == event::Receipt::tag()) {
      for(auto read_evt = evt.content().json().begin(); read_evt != evt.content().json().end(); ++read_evt) {
        const auto &obj = read_evt.value().toObject()["m.read"].toObject();
        for(auto user = obj.begin(); user != obj.end(); ++user) {
          update_receipt(UserID(user.key()), EventID(read_evt.key()), user.value().toObject()["ts"].toDouble());
        }
      }
      receipts_changed();
    } else if(evt.type() == event::Typing::tag()) {
      typing_ = event::Typing(evt).user_ids();
      typing_changed();
    } else {
      qDebug() << "Unrecognized ephemeral event type:" << evt.type().value();
    }
  }

  if(state_touched) {
    state_changed();
  }
  return state_touched;
}

bool RoomState::update_membership(const UserID &user_id, const event::room::MemberContent &content, Room *room,
                                  lmdb::dbi *member_db, lmdb::txn *txn) {
  auto id_utf8 = user_id.value().toUtf8();

  switch(content.membership()) {
  case Membership::INVITE:
  case Membership::JOIN: {
    auto &member = members_by_id_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(user_id),
        std::forward_as_tuple(user_id)).first->second;
    auto old_membership = member.membership();
    auto old_displayname = member.displayname();
    auto old_member_name = member_name(member);
    member.update_membership(content);
    if(member.displayname() != old_displayname) {
      if(old_displayname)
        forget_displayname(member.id(), *old_displayname, room);
      if(member.displayname())
        record_displayname(member.id(), *member.displayname(), room);
      if(room && membership_displayable(old_membership)) room->member_name_changed(member, old_member_name);
    }
    if(room && member.membership() != old_membership) {
      room->membership_changed(member, old_membership);
    }
    if(member_db) {
      auto data = QJsonDocument(member.json()).toBinaryData();
      lmdb::dbi_put(*txn, *member_db, lmdb::val(id_utf8.data(), id_utf8.size()),
                    lmdb::val(data.data(), data.size()));
    }
    break;
  }
  case Membership::LEAVE:
  case Membership::BAN: {
    if(room && user_id == room->id()) {
      room->left(content.membership());
    }
    auto it = members_by_id_.find(user_id);
    if(it != members_by_id_.end()) {
      auto &member = it->second;
      auto old_membership = member.membership();
      auto old_displayname = member.displayname();
      member.update_membership(content);
      if(member.displayname() != old_displayname) {
        if(old_displayname)
          forget_displayname(member.id(), *old_displayname, room);
        if(member.displayname())
           record_displayname(member.id(), *member.displayname(), room);
      }
      if(room) room->membership_changed(member, old_membership);
      assert(!departed_);
      departed_ = member.id();
    }
    if(member_db) {
      lmdb::dbi_del(*txn, *member_db, lmdb::val(id_utf8.data(), id_utf8.size()), nullptr);
    }
    break;
  }
  }
  return true;
}

bool RoomState::dispatch(const event::room::State &state, Room *room, lmdb::dbi *member_db, lmdb::txn *txn) {
  // This function must not have any side effects if a refining event's constructor throws!
  if(state.type() == event::room::Aliases::tag()) {
    std::unordered_set<QString, QStringHash> all_aliases;
    auto data = event::room::Aliases(state).aliases();  // FIXME: Need to validate these before using them
    all_aliases.reserve(aliases_.size() + data.size());

    std::move(aliases_.begin(), aliases_.end(), std::inserter(all_aliases, all_aliases.end()));
    aliases_.clear();

    std::transform(data.begin(), data.end(), std::inserter(all_aliases, all_aliases.end()),
                   [](const QJsonValue &v){ return v.toString(); });

    aliases_.reserve(all_aliases.size());
    std::move(all_aliases.begin(), all_aliases.end(), std::back_inserter(aliases_));
    if(room) room->aliases_changed();
    return true;
  }
  if(state.type() == event::room::CanonicalAlias::tag()) {
    event::room::CanonicalAlias ca{state};
    auto old = std::move(canonical_alias_);
    canonical_alias_ = ca.alias();
    if(room && canonical_alias_ != old) room->canonical_alias_changed();
    return true;
  }
  if(state.type() == event::room::Name::tag()) {
    event::room::Name n{state};
    auto old = std::move(name_);
    name_ = n.name();
    if(room && name_ != old) room->name_changed();
    return true;
  }
  if(state.type() == event::room::Topic::tag()) {
    event::room::Topic t{state};
    auto old = std::move(topic_);
    topic_ = t.topic();
    if(room && topic_ != old) {
      room->topic_changed(old);
    }
    return true;
  }
  if(state.type() == event::room::Avatar::tag()) {
    event::room::Avatar a(state);
    auto old = std::move(avatar_);
    avatar_ = QUrl(a.avatar());
    if(room && avatar_ != old) room->avatar_changed();
    return true;
  }
  if(state.type() == event::room::Create::tag()) {
    // Nothing to do here, because our rooms data structures are created implicitly
    return false;
  }
  if(state.type() == event::room::Member::tag()) {
    event::room::Member member(state);
    return update_membership(member.user(), member.content(), room, member_db, txn);
  }

  qDebug() << "Unrecognized message type:" << state.type().value();
  return false;
}

void RoomState::revert(const event::room::State &state) {
  if(state.type() == event::room::CanonicalAlias::tag()) {
    canonical_alias_ = event::room::CanonicalAlias(state).prev_alias();
    return;
  }
  if(state.type() == event::room::Name::tag()) {
    name_ = event::room::Name(state).prev_name();
    return;
  }
  if(state.type() == event::room::Topic::tag()) {
    topic_ = event::room::Topic(state).prev_topic();
    return;
  }
  if(state.type() == event::room::Avatar::tag()) {
    event::room::Avatar avatar(state);
    if(avatar.prev_avatar())
      avatar_ = QUrl(*avatar.prev_avatar());
    else
      avatar_ = QUrl();
    return;
  }
  if(state.type() == event::room::Member::tag()) {
    event::room::Member member(state);
    update_membership(member.user(),
                      member.prev_content().value_or(event::room::MemberContent::leave),
                      nullptr, nullptr, nullptr);
    prune_departed();
    return;
  }
}

void RoomState::ensure_member(const event::room::Member &e) {
  switch(e.content().membership()) {
  case Membership::LEAVE:
  case Membership::BAN: {
    auto r = members_by_id_.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(e.user()),
      std::forward_as_tuple(e.user()));
    if(!r.second) break;
    auto &member = r.first->second;
    if(e.prev_content()) {
      // Ensure that we get display name and avatar, if available
      member.update_membership(*e.prev_content());
    }
    member.update_membership(e.content());
    if(member.displayname()) record_displayname(member.id(), *member.displayname(), nullptr);
  }
  default:
    break;
  }
}

void RoomState::prune_departed(Room *room) {
  if(departed_) {
    auto dn = members_by_id_.at(*departed_).displayname();
    if(dn) forget_displayname(*departed_, *dn, room);
    members_by_id_.erase(*departed_);
    departed_ = {};
  }
}

MessageFetch *Room::get_messages(Direction dir, QString from, uint64_t limit, QString to) {
  QUrlQuery query;
  query.addQueryItem("from", from);
  query.addQueryItem("dir", dir == Direction::FORWARD ? "f" : "b");
  if(limit != 0) query.addQueryItem("limit", QString::number(limit));
  if(!to.isEmpty()) query.addQueryItem("to", to);
  auto reply = session_.get(QString("client/r0/rooms/" % QUrl::toPercentEncoding(id_.value()) % "/messages"), query);
  auto result = new MessageFetch(reply);
  connect(reply, &QNetworkReply::finished, [reply, result]() {
      auto r = decode(reply);
      if(r.error) {
        result->error(*r.error);
        return;
      }
      auto start_val = r.object["start"];
      if(!start_val.isString()) {
        result->error("invalid or missing \"start\" attribute in server's response");
        return;
      }
      auto start = start_val.toString();
      auto end_val = r.object["end"];
      if(!end_val.isString()) {
        result->error("invalid or missing \"end\" attribute in server's response");
        return;
      }
      auto end = end_val.toString();
      auto chunk_val = r.object["chunk"];
      if(!chunk_val.isArray()) {
        result->error("invalid or missing \"chunk\" attribute in server's response");
        return;
      }
      auto chunk = chunk_val.toArray();
      std::vector<event::Room> events;
      events.reserve(chunk.size());
      const char *error = nullptr;
      try {
        std::transform(chunk.begin(), chunk.end(), std::back_inserter(events),
                       [](const QJsonValue &v) { return event::Room(event::Identifiable(Event(v.toObject()))); });
      } catch(const malformed_event &e) {
        error = e.what();
      }
      if(error) {
        result->error(tr("malformed event: %1").arg(error));
      } else {
        result->finished(start, end, events);
      }
    });
  return result;
}

EventSend *Room::leave() {
  auto reply = session_.post(QString("client/r0/rooms/" % QUrl::toPercentEncoding(id_.value()) % "/leave"));
  auto es = new EventSend(reply);
  connect(reply, &QNetworkReply::finished, [es, reply]() {
      auto r = decode(reply);
      if(r.error) {
        es->error(*r.error);
      } else {
        es->finished();
      }
    });
  return es;
}

void Room::send(const QString &type, QJsonObject content) {
  pending_events_.push_back({type, content});
  transmit_event();
}

void Room::redact(const EventID &event, const QString &reason) {
  auto txn = session_.get_transaction_id();
  auto reply = session_.put("client/r0/rooms/" % QUrl::toPercentEncoding(id_.value()) % "/redact/" % QUrl::toPercentEncoding(event.value()) % "/" % txn,
                            reason.isEmpty() ? QJsonObject() : QJsonObject{{"reason", reason}}
    );
  auto es = new EventSend(reply);
  connect(reply, &QNetworkReply::finished, [reply, es]() {
      if(reply->error()) es->error(reply->errorString());
      else es->finished();
    });
  connect(es, &EventSend::error, this, &Room::error);
}

void Room::send_file(const QString &uri, const QString &name, const QString &media_type, size_t size) {
  send("m.room.message",
       {{"msgtype", "m.file"},
         {"url", uri},
         {"filename", name},
         {"body", name},
         {"info", QJsonObject{
             {"mimetype", media_type},
             {"size", static_cast<qint64>(size)}}}
           });
}

void Room::send_message(const QString &body) {
  send("m.room.message",
       {{"msgtype", "m.text"},
         {"body", body}});
}

void Room::send_emote(const QString &body) {
  send("m.room.message",
       {{"msgtype", "m.emote"},
         {"body", body}});
}

void Room::send_read_receipt(const EventID &event) {
  auto reply = session_.post(QString("client/r0/rooms/" % QUrl::toPercentEncoding(id_.value()) % "/receipt/m.read/" % QUrl::toPercentEncoding(event.value())));
  auto es = new EventSend(reply);
  connect(reply, &QNetworkReply::finished, [reply, es, event]() {
      if(reply->error()) {
        es->error(reply->errorString());
      } else {
        es->finished();
      }
    });
  connect(es, &EventSend::error, this, &Room::error);
}

gsl::span<const Room::Receipt * const> Room::receipts_for(const EventID &id) const {
  auto it = receipts_by_event_.find(id);
  if(it == receipts_by_event_.end()) return {};
  return gsl::span<const Room::Receipt * const>(static_cast<const Receipt * const *>(it->second.data()),
                                                  it->second.size());
}

const Room::Receipt *Room::receipt_from(const UserID &id) const {
  auto it = receipts_by_user_.find(id);
  if(it == receipts_by_user_.end()) return nullptr;
  return &it->second;
}

bool Room::has_unread() const {
  if(buffer().empty() || buffer().back().events.empty()) return true;
  auto r = receipt_from(session().user_id());
  if(!r) return true;
  for(auto batch = buffer().crbegin(); batch != buffer().crend(); ++batch) {
    for(auto event = batch->events.crbegin(); event != batch->events.crend(); ++event) {
      if(r->event == event->id()) return false;
      if(event->type() == event::room::Message::tag() && event->sender() != session().user_id()) return true;
    }
  }
  return true;
}

void Room::update_receipt(const UserID &user, const EventID &event, uint64_t ts) {
  const Receipt new_value{event, ts};
  auto emplaced = receipts_by_user_.emplace(user, new_value);
  if(!emplaced.second) {
    auto it = receipts_by_event_.find(emplaced.first->second.event);
    if(it != receipts_by_event_.end()) {
      auto &vec = it->second;
      // Remove from index
      vec.erase(std::remove(vec.begin(), vec.end(), &emplaced.first->second), vec.end());
    }
    emplaced.first->second = new_value;
  }
  receipts_by_event_[event].push_back(&emplaced.first->second);
}

void Room::transmit_event() {
  if(transmitting_) return;     // We'll be re-invoked when necessary by transmit_finished

  const auto &event = pending_events_.front();
  if(last_transmit_transaction_.isEmpty()) last_transmit_transaction_ = session_.get_transaction_id();
  transmitting_ = session_.put(
    "client/r0/rooms/" % QUrl::toPercentEncoding(id_.value()) % "/send/" % QUrl::toPercentEncoding(event.type) % "/" % last_transmit_transaction_,
    event.content);
  connect(transmitting_, &QNetworkReply::finished, this, &Room::transmit_finished);
}

void Room::transmit_finished() {
  using namespace std::chrono;
  using namespace std::chrono_literals;

  auto r = decode(transmitting_);
  transmitting_ = nullptr;
  bool retrying = false;
  if(r.code >= 400 && r.code < 500 && r.code != 429) {
    // HTTP client errors other than rate-limiting are unrecoverable
    error(*r.error);
    pending_events_.pop_front();
  } else if(!r.error) {
    pending_events_.pop_front();
  } else {
    retrying = true;
    qDebug() << "retrying send in" << duration_cast<duration<float>>(retry_backoff_).count() << "seconds due to error:" << *r.error;
  }
  if(!retrying) {
    last_transmit_transaction_ = "";
    retry_backoff_ = MINIMUM_BACKOFF;
  }
  if(!pending_events_.empty()) {
    if(retrying) {
      transmit_retry_timer_.start(duration_cast<milliseconds>(retry_backoff_).count());
      retry_backoff_ = std::min<steady_clock::duration>(30s, duration_cast<steady_clock::duration>(1.25 * retry_backoff_));
    } else {
      transmit_event();
    }
  }
}

}
