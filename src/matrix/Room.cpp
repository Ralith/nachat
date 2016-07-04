#include "Room.hpp"

#include <unordered_set>

#include "proto.hpp"

#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

#include "matrix.hpp"

namespace matrix {

QString Room::pretty_name() const {
  // Must be kept in sync with pretty_name_changed!
  if(!name_.isEmpty()) return name_;
  if(!canonical_alias_.isEmpty()) return canonical_alias_;
  if(!aliases_.empty()) return aliases_[0];  // Non-standard, but matches vector-web
  auto ms = members();
  ms.erase(std::remove_if(ms.begin(), ms.end(), [this](const Member *m){ return m->id() == user_id_; }), ms.end());
  if(ms.size() > 1) {
    std::partial_sort(ms.begin(), ms.begin() + 2, ms.end(),
                      [](const Member *a, const Member *b) {
                        return a->id() < b->id();
                      });
  }
  switch(ms.size()) {
  case 0: return tr("Empty Room");
  case 1: return member_name(*ms[0]);
  case 2: return tr("%1 and %2").arg(member_name(*ms[0])).arg(member_name(*ms[1]));
  default: return tr("%1 and %2 others").arg(member_name(*ms[0])).arg(ms.size() - 1);
  }
}

QString Room::member_name(const Member &member) const {
  if(member.display_name().isEmpty()) {
    return member.id();
  }
  if(members_by_displayname_.at(member.display_name()).size() == 1) {
    return member.display_name();
  }
  return tr("%1 (%2)").arg(member.display_name()).arg(member.id());
}

std::vector<const Member *> Room::members() const {
  std::vector<const Member *> result;
  result.reserve(members_by_id_.size());
  std::transform(members_by_id_.begin(), members_by_id_.end(), std::back_inserter(result),
                 [](auto &x) { return &x.second; });
  return result;
}

bool Room::forget_displayname(const Member &member) {
  bool member_name_changed = false;
  auto name = member.display_name();
  if(!name.isEmpty()) {
    auto &vec = members_by_displayname_.at(name);
    vec.erase(std::remove(vec.begin(), vec.end(), &member), vec.end());
    if(vec.empty()) {
      members_by_displayname_.erase(name);
    } else if(vec.size() == 1) {
      vec[0]->member_name_changed();
      member_name_changed = true;
    }
  }
  return member_name_changed;
}

void Room::dispatch(const proto::JoinedRoom &joined) {
  std::unordered_set<std::string> aliases;
  bool users_left = false;
  std::vector<const Member *> new_members;
  bool member_name_changed = false;
  for(auto &state : joined.state.events) {
    if(state.type == "m.room.aliases") {
      auto data = state.content["aliases"].toArray();
      aliases.reserve(aliases.size() + data.size());
      std::transform(data.begin(), data.end(), std::inserter(aliases, aliases.end()),
                     [](const QJsonValue &v){ return v.toString().toStdString(); });
    } else if(state.type == "m.room.canonical_alias") {
      canonical_alias_ = state.content["alias"].toString();
      if(name_.isEmpty()) {
        pretty_name_changed();
      }
    } else if(state.type == "m.room.name") {
      auto old_name = std::move(name_);
      name_ = state.content["name"].toString();
      if(name_ != old_name) {
        name_changed();
        pretty_name_changed();
      }
    } else if(state.type == "m.room.member") {
      const auto &user_id = state.state_key;
      auto membership = state.content["membership"].toString();
      if(membership == "join" || membership == "invite") {
        auto it = members_by_id_.find(user_id);
        bool fresh_member = false;
        if(it == members_by_id_.end()) {
          it = members_by_id_.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(user_id),
            std::forward_as_tuple(user_id)).first;
          new_members.push_back(&it->second);
          fresh_member = true;
        }
        auto &member = it->second;
        auto fresh_name = state.content["displayname"].toString();
        bool name_changed = false;
        if(!fresh_member && member.display_name() != fresh_name) {
          member_name_changed &= forget_displayname(member);
          name_changed = true;
        }
        member.dispatch(state);
        if(!member.display_name().isEmpty()) {
          auto &vec = members_by_displayname_[member.display_name()];
          vec.push_back(&member);
          if(vec.size() == 2) {
            vec[0]->member_name_changed();
            member_name_changed = true;
          }
        }
        if(name_changed) {
          member.member_name_changed();
          member_name_changed = true;
        }
      } else if(membership == "leave" || membership == "ban") {
        auto it = members_by_id_.find(user_id);
        if(it != members_by_id_.end()) {
          member_name_changed &= forget_displayname(it->second);
          members_by_id_.erase(user_id);
          users_left = true;
        }
      }

      if(name_.isEmpty() && canonical_alias_.isEmpty() && aliases_.empty()) {
        // May be spurious
        pretty_name_changed();
      }
    } else {
      qDebug() << tr("Unrecognized message type: ") << state.type;
    }
  }

  if(member_name_changed) {
    member_names_changed();
  }

  if(!aliases.empty()) {
    std::transform(aliases_.begin(), aliases_.end(), std::inserter(aliases, aliases.end()),
                   [](QString &s) { return std::move(s).toStdString(); });
    aliases_.clear();
    aliases_.reserve(aliases.size());
    std::transform(aliases.begin(), aliases.end(), std::back_inserter(aliases_),
                   [](const std::string &s) { return QString::fromStdString(s); });
    aliases_changed();
    if(name_.isEmpty() && canonical_alias_.isEmpty()) {
      // May be spurious
      pretty_name_changed();
    }
  }

  if(users_left || !new_members.empty()) {
    members_changed(new_members);
  }

  if(joined.unread_notifications.highlight_count != highlight_count_) {
    highlight_count_ = joined.unread_notifications.highlight_count;
    highlight_count_changed();
  }

  if(joined.unread_notifications.notification_count != notification_count_) {
    notification_count_ = joined.unread_notifications.notification_count;
    notification_count_changed();
  }
}

}
