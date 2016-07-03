#include "Room.hpp"

#include <unordered_set>

#include "proto.hpp"

#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

#include "matrix.hpp"

namespace matrix {

QString Room::pretty_name() const {
  if(!name_.isEmpty()) return name_;
  if(!aliases_.empty()) return aliases_[0];
  // TOOD: Fall back to membership list
  return id_;
}

std::vector<const User *> Room::users() const {
  return std::vector<const User *>(users_.begin(), users_.end());
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
      auto old_name = std::move(name_);
      name_ = state.content["name"].toString();
      if(name_ != old_name) name_changed();
    } else if(state.type == "m.room.member") {
      User &user = universe_.get_user(state.state_key);
      auto membership = state.content["membership"].toString();

      user.dispatch(state);

      if(membership == "join" || membership == "invite") {
        users_.insert(&user);
        new_users.push_back(&user);
      } else if(membership == "leave" || membership == "ban") {
        users_.erase(&user);
        users_left = true;
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
