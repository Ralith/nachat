#include "Room.hpp"

#include <unordered_set>

#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDebug>

#include "proto.hpp"
#include "Session.hpp"
#include "utils.hpp"
#include "parse.hpp"

namespace matrix {

QString RoomState::pretty_name(const QString &own_id) const {
  // Must be kept in sync with pretty_name_changed!
  if(!name_.isEmpty()) return name_;
  if(!canonical_alias_.isEmpty()) return canonical_alias_;
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
  case 1: return member_name(*ms[0]);
  case 2: return QObject::tr("%1 and %2").arg(member_name(*ms[0])).arg(member_name(*ms[1]));
  default: return QObject::tr("%1 and %2 others").arg(member_name(*ms[0])).arg(ms.size() - 1);
  }
}

QString RoomState::member_name(const Member &member) const {
  if(member.display_name().isEmpty()) {
    return member.id();
  }
  if(members_named(member.display_name()).size() == 1) {
    return member.display_name();
  }
  return QObject::tr("%1 (%2)").arg(member.display_name()).arg(member.id());
}

std::vector<Member *> &RoomState::members_named(QString displayname) {
  return members_by_displayname_.at(displayname.normalized(QString::NormalizationForm_C));
}
const std::vector<Member *> &RoomState::members_named(QString displayname) const {
  return members_by_displayname_.at(displayname.normalized(QString::NormalizationForm_C));
}

std::vector<const Member *> RoomState::members() const {
  std::vector<const Member *> result;
  result.reserve(members_by_id_.size());
  std::transform(members_by_id_.begin(), members_by_id_.end(), std::back_inserter(result),
                 [](auto &x) { return &x.second; });
  return result;
}

void RoomState::forget_displayname(const Member &member, QString old_name_in, Room *room) {
  if(!old_name_in.isEmpty()) {
    QString old_name = old_name_in.normalized(QString::NormalizationForm_C);
    auto &vec = members_named(old_name);
    QString other_name;
    const Member *other_member = nullptr;
    if(room && vec.size() == 2) {
      other_member = vec[0] == &member ? vec[1] : vec[0];
      other_name = member_name(*other_member);
    }
    vec.erase(std::remove(vec.begin(), vec.end(), &member), vec.end());
    if(vec.empty()) {
      members_by_displayname_.erase(old_name);
    }
    if(other_member) {
      room->member_name_changed(*other_member, other_name);
    }
  }
}

void RoomState::record_displayname(Member &member, Room *room) {
  if(member.display_name().isEmpty()) return;
  auto &vec = members_by_displayname_[member.display_name().normalized(QString::NormalizationForm_C)];
  Member *other_member = nullptr;
  QString other_old_name;
  if(vec.size() == 1) {
    // Third party existing name will become disambiguated
    other_member = vec[0];
    other_old_name = member_name(*vec[0]);
  }
  vec.push_back(&member);
  if(other_member && room) room->member_name_changed(*other_member, other_old_name);
}

const Member *RoomState::member(const QString &id) const {
  auto it = members_by_id_.find(id);
  if(it == members_by_id_.end()) return nullptr;
  return &it->second;
}

Room::Room(Matrix &universe, Session &session, QString id)
    : universe_(universe), session_(session), id_(id)
{}

QString Room::pretty_name() const {
  return state_.pretty_name(session_.user_id());
}

void Room::load_state(gsl::span<const proto::Event> events) {
  for(auto &state : events) {
    initial_state_.apply(state);
    state_.apply(state);
  }
}

void Room::dispatch(const proto::JoinedRoom &joined) {
  prev_batch(joined.timeline.prev_batch);

  bool state_touched = false;

  if(joined.unread_notifications.highlight_count != highlight_count_) {
    highlight_count_ = joined.unread_notifications.highlight_count;
    highlight_count_changed();
  }

  if(joined.unread_notifications.notification_count != notification_count_) {
    notification_count_ = joined.unread_notifications.notification_count;
    notification_count_changed();
  }

  Batch batch;
  batch.prev_batch = joined.timeline.prev_batch;
  batch.events.reserve(joined.timeline.events.size());
  for(auto &evt : joined.timeline.events) {
    state_touched |= state_.dispatch(evt, this);
    message(evt);
    batch.events.emplace_back(evt);
  }

  if(buffer_.size() >= session_.buffer_size() && session_.buffer_size() != 0) {
    for(auto &evt : buffer_.front().events) {
      initial_state_.apply(evt);
    }
    buffer_.pop_front();
  }
  buffer_.emplace_back(std::move(batch));

  state_.prune_departed_members(this);

  if(state_touched) state_changed();
}

bool RoomState::update_membership(const QString &user_id, const QJsonObject &content, Room *room) {
  auto membership = parse_membership(content["membership"].toString());
  if(!membership) {
    qDebug() << "Unrecognized membership type" << content["membership"].toString()
             << "in content" << content;
    return false;
  }
  switch(*membership) {
  case Membership::INVITE:
  case Membership::JOIN: {
    auto it = members_by_id_.find(user_id);
    bool new_member = false;
    if(it == members_by_id_.end()) {
      it = members_by_id_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(user_id),
        std::forward_as_tuple(user_id)).first;
      new_member = true;
    }
    auto &member = it->second;
    auto old_membership = member.membership();
    auto old_displayname = member.display_name();
    auto old_member_name = member_name(member);
    member.update_membership(content);
    if(member.display_name() != old_displayname) {
      forget_displayname(member, old_displayname, room);
      record_displayname(member, room);
      if(room && !new_member) room->member_name_changed(member, old_member_name);
    }
    if(room && member.membership() != old_membership) {
      room->membership_changed(it->second, *membership);
    }
    break;
  }
  case Membership::LEAVE:
  case Membership::BAN: {
    if(room && user_id == room->id()) {
      room->left(*membership);
    }
    auto it = members_by_id_.find(user_id);
    if(it != members_by_id_.end()) {
      it->second.update_membership(content);
      if(room) room->membership_changed(it->second, *membership);
    }
    break;
  }
  }
  return true;
}

bool RoomState::dispatch(const proto::Event &state, Room *room) {
  if(state.type == "m.room.message") return false;
  if(state.type == "m.room.aliases") {
    std::unordered_set<QString, QStringHash> all_aliases;
    auto data = state.content["aliases"].toArray();
    all_aliases.reserve(aliases_.size() + data.size());

    std::move(aliases_.begin(), aliases_.end(), std::inserter(all_aliases, all_aliases.end()));
    aliases_.clear();

    std::transform(data.begin(), data.end(), std::inserter(all_aliases, all_aliases.end()),
                   [](const QJsonValue &v){ return v.toString(); });

    aliases_.reserve(all_aliases.size());
    std::move(all_aliases.begin(), all_aliases.end(), std::back_inserter(aliases_));
    return true;
  }
  if(state.type == "m.room.canonical_alias") {
    canonical_alias_ = state.content["alias"].toString();
    return true;
  }
  if(state.type == "m.room.name") {
    name_ = state.content["name"].toString();
    return true;
  }
  if(state.type == "m.room.topic") {
    auto old = std::move(topic_);
    topic_ = state.content["topic"].toString();
    if(room && topic_ != old) {
      room->topic_changed(old);
    }
    return true;
  }
  if(state.type == "m.room.avatar") {
    avatar_ = QUrl(state.content["url"].toString());
    return true;
  }
  if(state.type == "m.room.create") {
    // Nothing to do here, because our rooms data structures are created implicitly
    return false;
  }
  if(state.type == "m.room.member") {
    return update_membership(state.state_key, state.content, room);
  }

  qDebug() << "Unrecognized message type:" << state.type;
  return false;
}

void RoomState::revert(const proto::Event &state) {
  if(state.type == "m.room.message") return;
  if(state.type == "m.room.canonical_alias") {
    canonical_alias_ = state.prev_content["alias"].toString();
    return;
  }
  if(state.type == "m.room.name") {
    name_ = state.prev_content["name"].toString();
    return;
  }
  if(state.type == "m.room.topic") {
    auto old = std::move(topic_);
    topic_ = state.prev_content["topic"].toString();
    return;
  }
  if(state.type == "m.room.avatar") {
    avatar_ = QUrl(state.prev_content["url"].toString());
    return;
  }
  if(state.type == "m.room.member") {
    update_membership(state.state_key, state.prev_content, nullptr);
    return;
  }
}

void RoomState::prune_departed_members(Room *room) {
  std::vector<std::pair<QString, const Member *>> departed_;
  for(const auto &member : members_by_id_) {
    if(member.second.membership() == Membership::LEAVE || member.second.membership() == Membership::BAN) {
      departed_.push_back(std::make_pair(member.first, &member.second));
    }
  }
  for(const auto &user : departed_) {
    forget_displayname(*user.second, user.second->display_name(), room);
    members_by_id_.erase(user.first);
  }
}

MessageFetch *Room::get_messages(Direction dir, QString from, uint64_t limit, QString to) {
  QUrlQuery query;
  query.addQueryItem("from", from);
  query.addQueryItem("dir", dir == Direction::FORWARD ? "f" : "b");
  if(limit != 0) query.addQueryItem("limit", QString::number(limit));
  if(!to.isEmpty()) query.addQueryItem("to", to);
  auto reply = session_.get("client/r0/rooms/" % id_ % "/messages", query);
  auto result = new MessageFetch(reply);
  connect(reply, &QNetworkReply::finished, [this, reply, result]() {
      auto r = decode(reply);
      reply->deleteLater();
      if(r.error) {
        result->error(*r.error);
        error(*r.error);
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
      std::vector<proto::Event> events;
      events.reserve(chunk.size());
      std::transform(chunk.begin(), chunk.end(), std::back_inserter(events), parse_event);
      result->finished(start, end, events);
    });
  return result;
}

void Room::leave() {
  auto reply = session_.post("client/r0/rooms/" % id_ % "/leave");
  connect(reply, &QNetworkReply::finished, [this, reply]() {
      reply->deleteLater();
      auto r = decode(reply);
      if(r.error) {
        error(*r.error);
      }
    });
}

void Room::send(const QString &type, QJsonObject content) {
  auto id = transaction_id_;
  transaction_id_ += 1;
  session_.put("client/r0/rooms/" % id_ % "/send/" % type % "/" % QString::number(id),
               content);
}

void Room::send_file(const QString &path) {
  auto f = new QFile(path, this);
  if(!f->open(QIODevice::ReadOnly)) {
    error(f->errorString());
    delete f;
    return;
  }
  auto reply = session_.post("media/r0/upload", f);
  connect(reply, &QNetworkReply::finished, [this, reply, f]() {
      QFileInfo info(*f);
      delete f;
      reply->deleteLater();
      auto r = decode(reply);
      if(r.error) {
        error(*r.error);
        return;
      }
      const QString uri = r.object["content_uri"].toString();
      send("m.room.message",
           {{"msgtype", "m.file"},  // Autodetect image/audio/video?
             {"url", uri},
             {"filename", info.fileName()},
             {"body", info.fileName()}});
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

}
