#include "proto.hpp"

#include <unordered_set>

namespace matrix {

using namespace proto;

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
  Timeline t{TimelineCursor{o["prev_batch"].toString()}};

  t.limited = o["limited"].toBool();
  t.events = parse_array(o["events"], [](QJsonValue v) {
      return event::Room(event::Identifiable(Event(v.toObject())));
    });

  return t;
}

JoinedRoom parse_joined_room(QString id, QJsonValue v) {
  auto o = v.toObject();
  JoinedRoom room{RoomID{id}, parse_timeline(o["timeline"])};

  auto un = o["unread_notifications"].toObject();
  room.unread_notifications.highlight_count = un["highlight_count"].toDouble();
  room.unread_notifications.notification_count = un["notification_count"].toDouble();
  room.state.events = parse_array(o["state"].toObject()["events"], [](QJsonValue v) {
      return event::room::State(event::Room(event::Identifiable(Event(v.toObject()))));
    });
  room.account_data.events = parse_array(o["account_data"].toObject()["events"], [](QJsonValue v) {
      return Event(v.toObject());
    });
  room.ephemeral.events = parse_array(o["ephemeral"].toObject()["events"], [](QJsonValue v) {
      return Event(v.toObject());
    });

  // Work around SYN-766
  std::unordered_set<EventID> event_ids;
  for(const auto &s : room.state.events) {
    event_ids.insert(s.id());
  }
  if(room.timeline.events.size() && event_ids.count(room.timeline.events.front().id())) {
    room.timeline.events = std::vector<event::Room>(room.timeline.events.begin() + 1, room.timeline.events.end());
  }

  return room;
}

Sync parse_sync(QJsonValue v) {
  auto o = v.toObject();
  Sync sync{SyncCursor{o["next_batch"].toString()}};
  QJsonObject::iterator i;

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
      sync.rooms.leave.emplace_back(RoomID{i.key()}, parse_timeline(i.value().toObject()["timeline"]));
    }
  }

  sync.presence.events = parse_array(o["presence"].toObject()["events"], [](QJsonValue v) {
      return Event(v.toObject());
    });

  return sync;
}

}
