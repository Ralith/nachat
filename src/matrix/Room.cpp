#include "Room.hpp"

#include <unordered_set>

#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

#include "proto.hpp"
#include "Session.hpp"

namespace matrix {

QString RoomState::pretty_name() const {
  // Must be kept in sync with pretty_name_changed!
  if(!name_.isEmpty()) return name_;
  if(!canonical_alias_.isEmpty()) return canonical_alias_;
  if(!aliases_.empty()) return aliases_[0];  // Non-standard, but matches vector-web
  auto ms = members();
  ms.erase(std::remove_if(ms.begin(), ms.end(), [this](const Member *m){ return m->id() == room_.session().user_id(); }), ms.end());
  if(ms.size() > 1) {
    std::partial_sort(ms.begin(), ms.begin() + 2, ms.end(),
                      [](const Member *a, const Member *b) {
                        return a->id() < b->id();
                      });
  }
  switch(ms.size()) {
  case 0: return QObject::tr("Empty Room");
  case 1: return member_name(*ms[0]);
  case 2: return QObject::tr("%1 and %2").arg(member_name(*ms[0])).arg(member_name(*ms[1]));
  default: return QObject::tr("%1 and %2 others").arg(member_name(*ms[0])).arg(ms.size() - 1);
  }
}

QString RoomState::member_name(const Member &member) const {
  if(member.display_name().isEmpty()) {
    return member.id();
  }
  if(members_by_displayname_.at(member.display_name()).size() == 1) {
    return member.display_name();
  }
  return QObject::tr("%1 (%2)").arg(member.display_name()).arg(member.id());
}

std::vector<const Member *> RoomState::members() const {
  std::vector<const Member *> result;
  result.reserve(members_by_id_.size());
  std::transform(members_by_id_.begin(), members_by_id_.end(), std::back_inserter(result),
                 [](auto &x) { return &x.second; });
  return result;
}

void RoomState::forget_displayname(const Member &member, QString old_name, bool timeline) {
  if(!old_name.isEmpty()) {
    auto &vec = members_by_displayname_.at(old_name);
    QString other_name;
    const Member *other_member = nullptr;
    if(timeline && vec.size() == 2) {
      other_member = vec[0] == &member ? vec[1] : vec[0];
      other_name = member_name(*other_member);
    }
    vec.erase(std::remove(vec.begin(), vec.end(), &member), vec.end());
    if(vec.empty()) {
      members_by_displayname_.erase(old_name);
    }
    if(other_member) {
      room_.member_name_changed(*other_member, other_name);
    }
  }
}

const Member *RoomState::member(const QString &id) const {
  auto it = members_by_id_.find(id);
  if(it == members_by_id_.end()) return nullptr;
  return &it->second;
}

Room::Room(Matrix &universe, Session &session, QString id)
    : universe_(universe), session_(session), id_(id), state_(*this)
{}

void Room::load_state(gsl::span<const proto::Event> events) {
  for(auto &state : events) {
    state_.dispatch(state, false);
  }
}

void Room::dispatch(const proto::JoinedRoom &joined) {
  bool state_touched = false;

  if(joined.unread_notifications.highlight_count != highlight_count_) {
    highlight_count_ = joined.unread_notifications.highlight_count;
    highlight_count_changed();
  }

  if(joined.unread_notifications.notification_count != notification_count_) {
    notification_count_ = joined.unread_notifications.notification_count;
    notification_count_changed();
  }

  for(auto &evt : joined.timeline.events) {
    qDebug() << "processing event type:" << evt.type;
    // TODO: Multiple message types, state change messages
    if(evt.type == "m.room.message") {
      if(auto member = state_.member(evt.sender)) {
        Message m{*member, evt.content["body"].toString()};
        message(m);
      } else {
        qDebug() << "dropping message from non-member" << evt.sender;
      }
    } else {
      state_touched |= state_.dispatch(evt, true);
    }
  }

  if(state_touched) state_changed();
}

bool RoomState::dispatch(const proto::Event &state, bool timeline) {
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
    topic_ = state.content["topic"].toString();
    return true;
  }
  if(state.type == "m.room.avatar") {
    avatar_ = QUrl(state.content["url"].toString());
    return true;
  }
  if(state.type == "m.room.member") {
    const auto &user_id = state.state_key;
    auto membership = parse_membership(state.content["membership"].toString());
    if(!membership) {
      qDebug() << "Unrecognized membership type" << state.content["membership"].toString();
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
      member.dispatch(state);
      if(member.display_name() != old_displayname) {
        forget_displayname(member, old_displayname, timeline);
        if(!member.display_name().isEmpty()) {
          auto &vec = members_by_displayname_[member.display_name()];
          Member *other_member = nullptr;
          QString other_old_name;
          if(vec.size() == 1) {
            // Third party existing name will become disambiguated
            other_member = vec[0];
            other_old_name = member_name(*vec[0]);
          }
          vec.push_back(&member);
          if(other_member && timeline) room_.member_name_changed(*other_member, other_old_name);
        }
        if(timeline && !new_member) room_.member_name_changed(member, old_member_name);
      }
      if(timeline && member.membership() != old_membership) {
        room_.membership_changed(it->second, *membership);
      }
      break;
    }
    case Membership::LEAVE:
    case Membership::BAN: {
      auto it = members_by_id_.find(user_id);
      if(it != members_by_id_.end()) {
        it->second.dispatch(state);
        if(timeline) room_.membership_changed(it->second, *membership);
        forget_displayname(it->second, it->second.display_name(), timeline);
        members_by_id_.erase(user_id);
      }
      break;
    }
    }
    return true;
  }

  qDebug() << "Unrecognized message type:" << state.type;
  return false;
}

}
