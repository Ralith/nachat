#ifndef NATIVE_CHAT_MATRIX_PROTO_HPP_
#define NATIVE_CHAT_MATRIX_PROTO_HPP_

#include <vector>
#include <experimental/optional>

#include <QString>
#include <QJsonObject>

#include "Event.hpp"

namespace matrix {

namespace proto {

struct Presence {
  std::vector<Event> events;
};

struct State {
  std::vector<Event> events;
};

struct Timeline {
  bool limited;
  QString prev_batch;
  std::vector<Event> events;
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
  QString id;
  UnreadNotifications unread_notifications;
  Timeline timeline;
  State state;
  AccountData account_data;
  Ephemeral ephemeral;
};

struct LeftRoom {
  QString id;
  Timeline timeline;
  State state;
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
  QString next_batch;
  Presence presence;
  Rooms rooms;
};

}

}

#endif
