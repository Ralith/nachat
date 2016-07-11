#ifndef NATIVE_CHAT_MATRIX_ROOM_H_
#define NATIVE_CHAT_MATRIX_ROOM_H_

#include <vector>
#include <unordered_map>
#include <deque>

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
  void apply(const proto::Event &e) { dispatch(e, nullptr); }
  void revert(const proto::Event &e);  // Reverts an event that, if a state event, has prev_content
  bool dispatch(const proto::Event &e, Room *room);
  // Returns true if changes were made. Emits state change events on room if supplied.

  const QString &name() const { return name_; }
  const QString &canonical_alias() const { return canonical_alias_; }
  gsl::span<const QString> aliases() const { return aliases_; }
  const QString &topic() const { return topic_; }
  const QUrl &avatar() const { return avatar_; }

  std::vector<const Member *> members() const;
  const Member *member(const QString &id) const;

  QString pretty_name(const QString &own_id) const;
  // Matrix r0.1.0 11.2.2.5 ish (like vector-web)

  QString member_name(const Member &member) const;
  // Matrix r0.1.0 11.2.2.3

  void prune_departed_members(Room *room);

private:
  QString name_;
  QString canonical_alias_;
  std::vector<QString> aliases_;
  QString topic_;
  QUrl avatar_;
  std::unordered_map<QString, Member, QStringHash> members_by_id_;
  std::unordered_map<QString, std::vector<Member *>, QStringHash> members_by_displayname_;

  void forget_displayname(const Member &member, QString old_name, Room *room);
  void record_displayname(Member &member, Room *room);
  std::vector<Member *> &members_named(QString displayname);
  const std::vector<Member *> &members_named(QString displayname) const;

  bool update_membership(const QString &user_id, const QJsonObject &content, Room *room);
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
  void error(QString message);
};

class Room : public QObject {
  Q_OBJECT

public:
  struct Batch {
    std::vector<proto::Event> events;
    QString prev_batch;
  };

  Room(Matrix &universe, Session &session, QString id);

  Room(const Room &) = delete;
  Room &operator=(const Room &) = delete;

  const Session &session() const { return session_; }
  const QString &id() const { return id_; }
  uint64_t highlight_count() const { return highlight_count_; }
  uint64_t notification_count() const { return notification_count_; }

  const RoomState &initial_state() const { return initial_state_; }
  const RoomState &state() const { return state_; }

  QString pretty_name() const;

  void load_state(gsl::span<const proto::Event>);
  void dispatch(const proto::JoinedRoom &);

  const std::deque<Batch> &buffer() { return buffer_; }

  enum class Direction { FORWARD, BACKWARD };

  MessageFetch *get_messages(Direction dir, QString from, uint64_t limit = 0, QString to = "");

signals:
  void membership_changed(const Member &, Membership old);
  void member_name_changed(const Member &, const QString &old);
  void state_changed();
  void highlight_count_changed();
  void notification_count_changed();
  void topic_changed(const QString &old);

  void prev_batch(const QString &);
  void message(const proto::Event &);

private:
  Matrix &universe_;
  Session &session_;
  const QString id_;

  RoomState initial_state_;
  std::deque<Batch> buffer_;
  RoomState state_;

  uint64_t highlight_count_ = 0, notification_count_ = 0;
};

}

#endif
