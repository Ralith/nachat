#ifndef NATIVE_CHAT_MATRIX_PROTO_HPP_
#define NATIVE_CHAT_MATRIX_PROTO_HPP_

#include <vector>

#include <QString>

#include "Event.hpp"
#include "ID.hpp"

class QJsonValue;

namespace matrix {

namespace proto {

struct Presence {
  std::vector<Event> events;
};

struct State {
  std::vector<event::room::State> events;
};

struct Timeline {
  bool limited;
  TimelineCursor prev_batch;
  std::vector<event::Room> events;

  explicit Timeline(TimelineCursor &&prev) : prev_batch{std::move(prev)} {}
};

struct UnreadNotifications {
  uint64_t highlight_count;
  uint64_t notification_count;
};

struct AccountData {
  std::vector<Event> events;
};

struct Ephemeral {
  std::vector<Event> events;
};

struct JoinedRoom {
  RoomID id;
  UnreadNotifications unread_notifications;
  Timeline timeline;
  State state;
  AccountData account_data;
  Ephemeral ephemeral;

  JoinedRoom(RoomID &&id, Timeline &&t) : id(std::move(id)), timeline(std::move(t)) {}
};

struct LeftRoom {
  RoomID id;
  Timeline timeline;
  State state;

  LeftRoom(RoomID &&id, Timeline &&timeline) : id(std::move(id)), timeline(std::move(timeline)) {}
};

struct InviteState {
  std::vector<Event> events;
};

struct InvitedRoom {
  InviteState invite_state;
};

struct Rooms {
  std::vector<JoinedRoom> join;
  std::vector<LeftRoom> leave;
  std::vector<InvitedRoom> invite;
};

struct Sync {
  SyncCursor next_batch;
  Presence presence;
  Rooms rooms;

  explicit Sync(SyncCursor &&next) : next_batch{std::move(next)} {}
};

}

proto::Sync parse_sync(QJsonValue v);

}

#endif
