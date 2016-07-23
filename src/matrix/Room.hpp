#ifndef NATIVE_CHAT_MATRIX_ROOM_H_
#define NATIVE_CHAT_MATRIX_ROOM_H_

#include <vector>
#include <unordered_map>
#include <deque>

#include <lmdb++.h>

#include <QString>
#include <QObject>
#include <QUrl>

#include <span.h>

#include "../QStringHash.hpp"

#include "Member.hpp"
#include "Event.hpp"

namespace matrix {

class Matrix;
class Session;

namespace proto {
struct JoinedRoom;
}

class RoomState {
public:
  RoomState() = default;  // New, empty room
  RoomState(const QJsonObject &state, lmdb::txn &txn, lmdb::dbi &members);  // Load from db

  void apply(const proto::Event &e) {
    dispatch(e, nullptr, nullptr, nullptr);
    prune_departed();
  }
  void revert(const proto::Event &e);  // Reverts an event that, if a state event, has prev_content

  void ensure_member(const proto::Event &e);
  // Recovers member whose membership is set to "leave" or "ban" by
  // e. Useful for allowing name disambiguation of departed members,
  // e.g. when stepping backwards.

  bool dispatch(const proto::Event &e, Room *room, lmdb::dbi *member_db, lmdb::txn *txn);
  // Returns true if changes were made. Emits state change events on room if supplied.

  const QString &name() const { return name_; }
  const QString &canonical_alias() const { return canonical_alias_; }
  gsl::span<const QString> aliases() const { return aliases_; }
  const QString &topic() const { return topic_; }
  const QUrl &avatar() const { return avatar_; }

  std::vector<const Member *> members() const;
  const Member *member_from_id(const MemberID &id) const;

  QString pretty_name(const QString &own_id) const;
  // Matrix r0.1.0 11.2.2.5 ish (like vector-web)

  QString member_name(const Member &member) const;
  // Matrix r0.1.0 11.2.2.3

  void prune_departed(Room *room = nullptr);

  QJsonObject to_json() const;
  // For serialization

private:
  QString name_;
  QString canonical_alias_;
  std::vector<QString> aliases_;
  QString topic_;
  QUrl avatar_;
  std::unordered_map<MemberID, Member, QStringHash> members_by_id_;
  std::unordered_map<QString, std::vector<MemberID>, QStringHash> members_by_displayname_;
  MemberID departed_;

  void forget_displayname(const MemberID &member, const QString &old_name, Room *room);
  void record_displayname(const MemberID &member, const QString &name, Room *room);
  std::vector<MemberID> &members_named(QString displayname);
  const std::vector<MemberID> &members_named(QString displayname) const;

  bool update_membership(const QString &user_id, const QJsonObject &content, Room *room, lmdb::dbi *member_db, lmdb::txn *txn);
};

struct Batch {
  std::vector<matrix::proto::Event> events;
  QString next, prev;
};

class MessageFetch : public QObject {
  Q_OBJECT

public:
  MessageFetch(QObject *parent = nullptr) : QObject(parent) {}

signals:
  void finished(QString start, QString end, gsl::span<const proto::Event> events);
  void error(const QString &message);
};

class EventSend : public QObject {
  Q_OBJECT

public:
  EventSend(QObject *parent = nullptr) : QObject(parent) {}

signals:
  void finished();
  void error(const QString &message);
};

class Room : public QObject {
  Q_OBJECT

public:
  struct Batch {
    std::vector<proto::Event> events;
    QString prev_batch;
  };

  Room(Matrix &universe, Session &session, QString id, const QJsonObject &initial,
       lmdb::env &env, lmdb::txn &init_txn, lmdb::dbi &&member_db);

  Room(const Room &) = delete;
  Room &operator=(const Room &) = delete;

  const Session &session() const { return session_; }
  Session &session() { return session_; }
  const QString &id() const { return id_; }
  uint64_t highlight_count() const { return highlight_count_; }
  uint64_t notification_count() const { return notification_count_; }

  const RoomState &initial_state() const { return initial_state_; }
  const RoomState &state() const { return state_; }

  QString pretty_name() const;

  void load_state(lmdb::txn &txn, gsl::span<const proto::Event>);
  bool dispatch(lmdb::txn &txn, const proto::JoinedRoom &);

  const std::deque<Batch> &buffer() { return buffer_; }
  size_t buffer_size() const;

  QJsonObject to_json() const;

  enum class Direction { FORWARD, BACKWARD };

  MessageFetch *get_messages(Direction dir, QString from, uint64_t limit = 0, QString to = "");

  void leave();

  void send(const QString &type, QJsonObject content);

  void send_file(const QString &path);
  void send_message(const QString &body);
  void send_emote(const QString &body);

signals:
  void membership_changed(const Member &, Membership old);
  void member_name_changed(const Member &, const QString &old);
  void state_changed();
  void highlight_count_changed();
  void notification_count_changed();
  void name_changed();
  void canonical_alias_changed();
  void aliases_changed();
  void topic_changed(const QString &old);
  void avatar_changed();
  void discontinuity();

  void prev_batch(const QString &);
  void message(const proto::Event &);

  void error(const QString &msg);
  void left(Membership reason);

private:
  Matrix &universe_;
  Session &session_;
  const QString id_;
  lmdb::env &db_env_;
  lmdb::dbi member_db_;

  RoomState initial_state_;
  std::deque<Batch> buffer_;
  RoomState state_;

  uint64_t highlight_count_ = 0, notification_count_ = 0;
  uint64_t transaction_id_ = 0;
};

}

#endif
