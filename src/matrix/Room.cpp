#include "Room.hpp"

#include <unordered_set>
#include <memory>
#include <algorithm>

#include <QtNetwork>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QtDebug>

#include "proto.hpp"
#include "Session.hpp"
#include "utils.hpp"

using std::experimental::optional;

namespace matrix {

RoomState::RoomState(const QJsonObject &info, gsl::span<const Member> members) {
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

  const auto as = info["aliases"].toArray();
  aliases_.reserve(as.size());
  std::transform(as.begin(), as.end(), std::back_inserter(aliases_),
                 [](const QJsonValue &v) {
                   return v.toString();
                 });

  members_by_id_.reserve(members.size());
  for(const auto &member : members) {
    members_by_id_.insert(member);
    if(member.second.displayname()) {
      record_displayname(member.first, *member.second.displayname(), nullptr);
    }
  }
}

QJsonObject RoomState::to_json() const {
  // Members are omitted because they're stored in a dedicated per-room database for efficient incremental update

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
  auto ms = members();
  ms.erase(std::remove_if(ms.begin(), ms.end(), [&](const Member *m){
        return m->first == own_id;
      }), ms.end());
  if(ms.size() > 1) {
    std::partial_sort(ms.begin(), ms.begin() + 2, ms.end(),
                      [](const Member *a, const Member *b) {
                        return a->first < b->first;
                      });
  }
  switch(ms.size()) {
  case 0: return Room::tr("Empty room");
  case 1: return matrix::pretty_name(ms[0]->first, ms[0]->second);
  case 2: return Room::tr("%1 and %2").arg(member_name(ms[0]->first)).arg(member_name(ms[1]->first));
  default: return Room::tr("%1 and %2 others").arg(member_name(ms[0]->first)).arg(ms.size() - 1);
  }
}

optional<QString> RoomState::member_disambiguation(const UserID &member_id) const {
  const auto &member = members_by_id_.at(member_id);
  if(!member.displayname()) return {};
  if(members_named(*member.displayname()).size() > 1
     || (member.displayname() && member_from_id(UserID(member.displayname()->normalized(QString::NormalizationForm_C))))) {
    return member_id.value();
  } else {
    return {};
  }
}

optional<QString> RoomState::nonmember_disambiguation(const UserID &id, const QString &displayname) const {
  if(members_by_id_.find(UserID{displayname}) != members_by_id_.end()
     || members_by_displayname_.find(displayname) != members_by_displayname_.end()) {
    return id.value();
  }
  return {};
}

QString RoomState::member_name(const UserID &member) const {
  auto result = matrix::pretty_name(member, members_by_id_.at(member));
  auto disambig = member_disambiguation(member);
  if(!disambig) return result;
  return result % " (" % *disambig % ")";
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
                 [](auto &x) { return &x; });
  return result;
}

void RoomState::forget_displayname(const UserID &id, const QString &old_name_in, Room *room) {
  const QString old_name = old_name_in.normalized(QString::NormalizationForm_C);
  auto &vec = members_by_displayname_.at(old_name);
  QString other_disambiguation;
  optional<UserID> other_member;
  const bool existing_displayname = vec.size() == 2;
  const bool existing_mxid = member_from_id(UserID(old_name));
  if(room && (!existing_displayname || !existing_mxid)) {
    if(existing_displayname) other_member = vec[0] == id ? vec[1] : vec[0];
    if(existing_mxid) other_member = UserID(old_name);
  }
  if(other_member) {
    room->member_disambiguation_changed(*other_member, other_member->value(), {});
  }
  const auto before = vec.size();
  vec.erase(std::remove(vec.begin(), vec.end(), id), vec.end());
  assert(before - vec.size() == 1);
  if(vec.empty()) {
    members_by_displayname_.erase(old_name);
  }
  if(other_member) {
    
  }
}

void RoomState::record_displayname(const UserID &id, const QString &name, Room *room) {
  const auto normalized = name.normalized(QString::NormalizationForm_C);
  auto &vec = members_by_displayname_[normalized];
  for(const auto &x : vec) {
    assert(x != id);
  }

  if(room && vec.size() == 1) {
    room->member_disambiguation_changed(vec[0], {}, vec[0].value());
  }

  vec.push_back(id);
}

const event::room::MemberContent *RoomState::member_from_id(const UserID &id) const {
  auto it = members_by_id_.find(id);
  if(it == members_by_id_.end()) return nullptr;
  return &it->second;
}

static constexpr std::chrono::steady_clock::duration MINIMUM_BACKOFF(std::chrono::seconds(5));
// Default synapse seconds-per-message when throttled

Room::Room(Matrix &universe, Session &session, RoomID id, const QJsonObject &initial,
           gsl::span<const Member> members)
    : universe_(universe), session_(session), id_(std::move(id)),
      state_{initial["state"].toObject(), members},
      last_batch_{initial["last_batch"].toObject()},
      highlight_count_{static_cast<uint64_t>(initial["highlight_count"].toDouble(0))},
      notification_count_{static_cast<uint64_t>(initial["notification_count"].toDouble(0))},
      transmitting_(nullptr), retry_backoff_(MINIMUM_BACKOFF)
{
  transmit_retry_timer_.setSingleShot(true);
  connect(&transmit_retry_timer_, &QTimer::timeout, this, &Room::transmit_event);

  auto receipts = initial["receipts"].toObject();
  for(auto it = receipts.begin(); it != receipts.end(); ++it) {
    update_receipt(UserID(it.key()), EventID(it.value().toObject()["event_id"].toString()), it.value().toObject()["ts"].toDouble());
  }
}

Room::Room(Matrix &universe, Session &session, const proto::JoinedRoom &joined_room)
    : universe_(universe), session_(session), id_{joined_room.id},
      transmitting_(nullptr), retry_backoff_(MINIMUM_BACKOFF)
{
  transmit_retry_timer_.setSingleShot(true);
  connect(&transmit_retry_timer_, &QTimer::timeout, this, &Room::transmit_event);

  dispatch(joined_room);
}

QString Room::pretty_name() const {
  return state_.pretty_name(session_.user_id());
}

static std::vector<event::Room> parse_room_events(const QJsonArray &a) {
  std::vector<event::Room> result;
  result.reserve(a.size());
  for(const auto &e : a) {
    result.emplace_back(event::Room{event::Identifiable{Event{e.toObject()}}});
  }
  return result;
}

Batch::Batch(const proto::Timeline &t) : Batch{t.prev_batch, t.events} {}
Batch::Batch(const QJsonObject &o) : Batch{TimelineCursor{o["begin"].toString()}, parse_room_events(o["events"].toArray())} {}

QJsonObject Batch::to_json() const {
  QJsonArray a;
  for(const auto &e : events) {
    a.push_back(e.json());
  }
  return QJsonObject{{"begin", begin.value()}, {"events", std::move(a)}};
}

QJsonObject Room::to_json() const {
  QJsonObject o;
  QJsonObject receipts;
  for(const auto &receipt : receipts_by_user_) {
    receipts[receipt.first.value()] = QJsonObject{{"event_id", receipt.second.event.value()}, {"ts", static_cast<qint64>(receipt.second.ts)}};
  }
  return QJsonObject{
    {"state", state_.to_json()},
    {"highlight_count", static_cast<double>(highlight_count_)},
    {"notification_count", static_cast<double>(notification_count_)},
    {"receipts", receipts},
    {"last_batch", last_batch().to_json()}
  };
}

bool Room::dispatch(const proto::JoinedRoom &joined) {
  bool state_touched = false;

  for(auto &state : joined.state.events) {
    try {
      state_touched |= state_.dispatch(state, this);
    } catch(malformed_event &e) {
      qWarning() << "WARNING:" << id().value() << "ignoring malformed state:" << e.what() << state.json();
    }
  }

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

  prev_batch(joined.timeline.prev_batch);

  for(auto &evt : joined.timeline.events) {
    message(evt);

    if(auto s = evt.to_state()) {
      try {
        state_touched |= state_.dispatch(*s, this);
      } catch(const malformed_event &e) {
        qWarning() << "WARNING:" << id().value() << "ignoring malformed state:" << e.what() << s->json();
      }
    }
  }

  batch(joined.timeline);

  for(const auto &evt : joined.ephemeral.events) {
    if(evt.type() == event::Receipt::tag()) {
      const auto content = evt.content().json();
      for(auto read_evt = content.begin(); read_evt != content.end(); ++read_evt) {
        const auto obj = read_evt.value().toObject()["m.read"].toObject();
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

  last_batch_.emplace(joined.timeline);

  return state_touched;
}

bool RoomState::update_membership(const UserID &user_id, const event::room::MemberContent &content, Room *room) {
  auto it = members_by_id_.find(user_id);
  if(room) {
    room->member_changed(user_id, it != members_by_id_.end() ? it->second : event::room::MemberContent::leave, content);
  }

  switch(content.membership()) {
  case Membership::INVITE:
  case Membership::JOIN: {
    if(it == members_by_id_.end()) {
      it = members_by_id_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(user_id),
        std::forward_as_tuple(event::room::MemberContent::leave)).first;
    }
    auto &member = it->second;;
    if(content.displayname() != member.displayname()) {
      if(member.displayname())
        forget_displayname(user_id, *member.displayname(), room);
      if(content.displayname())
        record_displayname(user_id, *content.displayname(), room);
    }
    member = content;
    break;
  }
  case Membership::LEAVE:
  case Membership::BAN: {
    if(room && user_id == room->id()) {
      room->left(content.membership());
    }
    if(it != members_by_id_.end()) {
      auto &member = it->second;
      if(member.displayname()) {
        forget_displayname(user_id, *member.displayname(), room);
      }
      members_by_id_.erase(it);
    }
    break;
  }
  }
  return true;
}

bool RoomState::dispatch(const event::room::State &state, Room *room) {
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
    name_ = n.content().name();
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
    return update_membership(member.user(), member.content(), room);
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
    auto c = event::room::Name(state).prev_content();
    if(c) {
      name_ = c->name();
    } else {
      name_ = {};
    }
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
                      nullptr);
    return;
  }
}

MessageFetch *Room::get_messages(Direction dir, const TimelineCursor &from, uint64_t limit, optional<TimelineCursor> to) {
  QUrlQuery query;
  query.addQueryItem("from", from.value());
  query.addQueryItem("dir", dir == Direction::FORWARD ? "f" : "b");
  if(limit != 0) query.addQueryItem("limit", QString::number(limit));
  if(to) query.addQueryItem("to", to->value());
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
      TimelineCursor start{start_val.toString()};

      auto end_val = r.object["end"];
      if(!end_val.isString()) {
        result->error("invalid or missing \"end\" attribute in server's response");
        return;
      }
      TimelineCursor end{end_val.toString()};

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

TransactionID Room::send(const EventType &type, event::Content content) {
  pending_events_.push_back({session_.get_transaction_id(), type, std::move(content)});
  transmit_event();
  return pending_events_.back().transaction_id;
}

TransactionID Room::redact(const EventID &event, const QString &reason) {
  auto txn = session_.get_transaction_id();
  auto reply = session_.put(QString{"client/r0/rooms/" % QUrl::toPercentEncoding(id_.value())
        % "/redact/" % QUrl::toPercentEncoding(event.value()) % "/" % QUrl::toPercentEncoding(txn.value())},
                            reason.isEmpty() ? QJsonObject() : QJsonObject{{"reason", reason}}
    );
  QPointer<Room> self(this);
  connect(reply, &QNetworkReply::finished, [self, reply]() {
      // TODO: Retry
      if(self && reply->error()) self->error(reply->errorString());
    });
  return txn;
}

TransactionID Room::send_file(const QString &uri, const QString &name, const QString &media_type, size_t size) {
  return send(event::room::Message::tag(),
              event::Content{{
                    {"msgtype", "m.file"},
                    {"url", uri},
                    {"filename", name},
                    {"body", name},
                    {"info", QJsonObject{
                        {"mimetype", media_type},
                        {"size", static_cast<qint64>(size)}}}
                }});
}

TransactionID Room::send_message(const QString &body) {
  return send(event::room::Message::tag(),
              event::Content{{
                  {"msgtype", "m.text"},
                  {"body", body}}});
}

TransactionID Room::send_emote(const QString &body) {
  return send(event::room::Message::tag(),
              event::Content{{
                  {{"msgtype", "m.emote"},
                    {"body", body}}}});
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

void Room::update_receipt(const UserID &user, const EventID &event, uint64_t ts) {
  const Receipt new_value{event, ts};
  auto emplaced = receipts_by_user_.emplace(user, new_value);
  if(!emplaced.second) {
    auto it = receipts_by_event_.find(emplaced.first->second.event);
    if(it != receipts_by_event_.end()) {
      auto &vec = it->second;
      // Remove from index
      vec.erase(std::remove(vec.begin(), vec.end(), &emplaced.first->second), vec.end());
      if(vec.empty()) {
        receipts_by_event_.erase(it);
      }
    }
    emplaced.first->second = new_value;
  }
  receipts_by_event_[event].push_back(&emplaced.first->second);
}

void Room::transmit_event() {
  if(transmitting_) return;     // We'll be re-invoked when necessary by transmit_finished

  const auto &event = pending_events_.front();
  transmitting_ = session_.put(QString{"client/r0/rooms/" % QUrl::toPercentEncoding(id_.value())
        % "/send/" % QUrl::toPercentEncoding(event.type.value()) % "/" % QUrl::toPercentEncoding(event.transaction_id.value())},
    event.content.json());
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
