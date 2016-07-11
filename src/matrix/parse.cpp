#include "parse.hpp"

#include <QJsonArray>

namespace matrix {

using namespace proto;

Unsigned parse_unsigned(QJsonValue v) {
  auto o = v.toObject();
  Unsigned u;

  auto i = o.find("prev_content");
  if(i != o.end()) {
    u.prev_content = i->toObject();
  }
  u.age = o["age"].toDouble();
  i = o.find("transaction_id");
  if(i != o.end()) {
    u.transaction_id = i->toString();
  }

  return u;
}

Event parse_event(QJsonValue v) {
  auto o = v.toObject();
  Event e;

  e.content = o["content"].toObject();
  e.prev_content = o["prev_content"].toObject();
  e.origin_server_ts = o["origin_server_ts"].toDouble();
  e.sender = o["sender"].toString();
  e.type = o["type"].toString();
  e.unsigned_ = parse_unsigned(o["unsigned"]);
  e.state_key = o["state_key"].toString();

  return e;
}

template<typename F>
std::vector<std::result_of_t<F(QJsonValue)>> parse_array(QJsonValue v, F &&f) {
  auto a = v.toArray();
  std::vector<std::result_of_t<F(QJsonValue)>> out;
  out.reserve(a.size());
  std::transform(a.begin(), a.end(), std::back_inserter(out), std::forward<F>(f));
  return out;
}

Timeline parse_timeline(QJsonValue v) {
  auto o = v.toObject();
  Timeline t;

  t.limited = o["limited"].toBool();
  t.prev_batch = o["prev_batch"].toString();
  t.events = parse_array(o["events"], parse_event);

  return t;
}

JoinedRoom parse_joined_room(QString id, QJsonValue v) {
  auto o = v.toObject();
  JoinedRoom room;

  room.id = id;
  room.timeline = parse_timeline(o["timeline"]);

  auto un = o["unread_notifications"].toObject();
  room.unread_notifications.highlight_count = un["highlight_count"].toDouble();
  room.unread_notifications.notification_count = un["notification_count"].toDouble();
  room.state.events = parse_array(o["state"].toObject()["events"], parse_event);
  room.account_data.events = parse_array(o["account_data"].toObject()["events"], parse_event);
  room.ephemeral.events = parse_array(o["ephemeral"].toObject()["events"], parse_event);

  return room;
}

LeftRoom parse_left_room(QString id, QJsonValue v) {
  auto o = v.toObject();
  LeftRoom room;

  room.id = id;
  room.timeline = parse_timeline(o["timeline"]);

  return room;
}

Sync parse_sync(QJsonValue v) {
  auto o = v.toObject();
  Sync sync;
  QJsonObject::iterator i;

  sync.next_batch = o["next_batch"].toString();

  {
    auto rooms = o["rooms"].toObject();

    auto join = rooms["join"].toObject();
    sync.rooms.join.reserve(join.size());
    for(auto i = join.begin(); i != join.end(); ++i) {
      sync.rooms.join.push_back(parse_joined_room(i.key(), i.value()));
    }

    auto leave = rooms["leave"].toObject();
    sync.rooms.leave.reserve(leave.size());
    for(auto i = leave.begin(); i != leave.end(); ++i) {
      sync.rooms.leave.push_back(parse_left_room(i.key(), i.value()));
    }
  }

  sync.presence.events = parse_array(o["presence"].toObject()["events"], parse_event);

  return sync;
}

}
