#include "Room.hpp"

#include <unordered_set>

#include "proto.hpp"

#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

namespace matrix {

void User::dispatch(const proto::Event &state) {
  auto membership = state.content["membership"].toString();
  bool old_invite_pending = invite_pending_;
  if(membership == "invite") {
    invite_pending_ = true;
  } else if(membership == "join") {
    invite_pending_ = false;
  }
  if(invite_pending_ != old_invite_pending) {
    invite_pending_changed();
  }

  auto i = state.content.find("displayname");
  if(i != state.content.end()) {
    display_name_ = i->toString();
    display_name_changed();
  }
  i = state.content.find("avatar_url");
  if(i != state.content.end()) {
    avatar_url_ = QUrl(i->toString());
    avatar_url_changed();
  }
}

QString Room::display_name() const {
  if(name_) return *name_;
  if(!aliases_.empty()) return aliases_[0];
  // TOOD: Fall back to membership list
  return id_;
}

void Room::dispatch(const proto::JoinedRoom &joined) {
  std::unordered_set<std::string> aliases;
  bool users_left = false;
  std::vector<const User *> new_users;
  for(auto &state : joined.state.events) {
    if(state.type == "m.room.aliases") {
      auto data = state.content["aliases"].toArray();
      aliases.reserve(aliases.size() + data.size());
      std::transform(data.begin(), data.end(), std::inserter(aliases, aliases.end()),
                     [](const QJsonValue &v){ return v.toString().toStdString(); });
    } else if(state.type == "m.room.name") {
      name_ = state.content["name"].toString();
      name_changed();
    } else if(state.type == "m.room.member") {
      auto key = state.state_key.toStdString();
      auto it = users_.find(key);
      auto membership = state.content["membership"].toString();
      if(it == users_.end() && (membership == "join" || membership == "invite")) {
        it = users_.emplace(std::piecewise_construct,
                            std::forward_as_tuple(key),
                            std::forward_as_tuple(state.state_key)).first;
      }
      if(it != users_.end()) {
        it->second.dispatch(state);

        if(membership == "leave" || membership == "ban") {
          users_.erase(key);
          users_left = true;
        }
      }
    } else {
      qDebug() << tr("Unrecognized message type: ") << state.type;
    }
  }

  if(!aliases.empty()) {
    std::transform(aliases_.begin(), aliases_.end(), std::inserter(aliases, aliases.end()),
                   [](QString &s) { return std::move(s).toStdString(); });
    aliases_.clear();
    aliases_.reserve(aliases.size());
    std::transform(aliases.begin(), aliases.end(), std::back_inserter(aliases_),
                   [](const std::string &s) { return QString::fromStdString(s); });
    aliases_changed();
  }

  if(users_left || !new_users.empty()) {
    users_changed(new_users);
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
