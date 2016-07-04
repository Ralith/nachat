#ifndef NATIVE_CHAT_MATRIX_ROOM_H_
#define NATIVE_CHAT_MATRIX_ROOM_H_

#include <vector>
#include <deque>
#include <unordered_set>
#include <unordered_map>

#include <QString>
#include <QObject>
#include <QUrl>

#include <span.h>

#include "../QStringHash.hpp"

#include "Member.hpp"

namespace matrix {

class Matrix;

namespace proto {
struct JoinedRoom;
}

struct Message {
  const Member &sender;
  QString body;
};

struct StateID {
  QString type, key;
};

struct SIDHash {
  size_t operator()(const StateID &sid) const { return qHash(sid.type) ^ qHash(sid.key); }
};

enum class Membership {
  INVITE, JOIN, LEAVE, BAN
};

class RoomState {
public:
  RoomState(const QString &user_id) : user_id_(user_id) {}

  bool apply(const proto::Event &e);
  // Returns true if changes were made

  const QString &name() const { return name_; }
  const QString &canonical_alias() const { return canonical_alias_; }
  gsl::span<const QString> aliases() const { return aliases_; }
  const QString &topic() const { return topic_; }
  const QUrl &avatar() const { return avatar_; }

  std::vector<const Member *> members() const;
  const Member *member(const QString &id) const;

  QString pretty_name() const;
  // Matrix r0.1.0 11.2.2.5 ish (like vector-web)

  QString member_name(const Member &member) const;
  // Matrix r0.1.0 11.2.2.3

private:
  const QString &user_id_;
  QString name_;
  QString canonical_alias_;
  std::vector<QString> aliases_;
  QString topic_;
  QUrl avatar_;
  std::unordered_map<QString, Member, QStringHash> members_by_id_;
  std::unordered_map<QString, std::vector<Member *>, QStringHash> members_by_displayname_;

  void forget_displayname(const Member &member);
};

class Room : public QObject {
  Q_OBJECT

public:
  using MessageList = std::deque<Message>;

  Room(Matrix &universe, QString user_id, QString id)
      : universe_(universe), id_(id), initial_state_(user_id), current_state_(user_id)
  {}

  Room(const Room &) = delete;
  Room &operator=(const Room &) = delete;

  const QString &id() const { return id_; }
  const MessageList &messages() const { return messages_; }
  uint64_t highlight_count() const { return highlight_count_; }
  uint64_t notification_count() const { return notification_count_; }

  const RoomState &initial_state() const { return initial_state_; }
  const RoomState &current_state() const { return current_state_; }

  void dispatch(const proto::JoinedRoom &);

signals:
  void state_changed();
  void message(const Message &);
  void backlog_grew(MessageList::iterator end);
  void highlight_count_changed();
  void notification_count_changed();

private:
  Matrix &universe_;
  const QString id_;

  bool uninitialized_ = true;

  RoomState initial_state_;
  MessageList messages_;
  uint64_t message_count_;
  RoomState current_state_;
  // current_state_ = foldr apply initial_state_ messages_

  uint64_t highlight_count_ = 0, notification_count_ = 0;
};

}

#endif
