#ifndef NATIVE_CHAT_MATRIX_ROOM_H_
#define NATIVE_CHAT_MATRIX_ROOM_H_

#include <vector>
#include <unordered_map>
#include <deque>
#include <chrono>

#include <lmdb++.h>

#include <QString>
#include <QObject>
#include <QUrl>
#include <QTimer>

#include <span.h>

#include "../QStringHash.hpp"

#include "Member.hpp"
#include "Event.hpp"

class QNetworkReply;

namespace matrix {

class Matrix;
class Session;

namespace proto {
struct JoinedRoom;
}

using RoomID = QString;
using EventID = QString;

class RoomState {
public:
  RoomState() = default;  // New, empty room
  RoomState(const QJsonObject &state, lmdb::txn &txn, lmdb::dbi &members);  // Load from db

  void apply(const proto::Event &e) {
    dispatch(e, nullptr, nullptr, nullptr);
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
  const Member *member_from_id(const UserID &id) const;

  QString pretty_name(const QString &own_id) const;
  // Matrix r0.1.0 11.2.2.5 ish (like vector-web)

  QString member_disambiguation(const Member &member) const;
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
  std::unordered_map<UserID, Member, QStringHash> members_by_id_;
  std::unordered_map<QString, std::vector<UserID>, QStringHash> members_by_displayname_;
  UserID departed_;

  void forget_displayname(const UserID &member, const QString &old_name, Room *room);
  void record_displayname(const UserID &member, const QString &name, Room *room);
  std::vector<UserID> &members_named(QString displayname);
  const std::vector<UserID> &members_named(QString displayname) const;

  bool update_membership(const QString &user_id, const QJsonObject &content, Room *room, lmdb::dbi *member_db, lmdb::txn *txn);
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

  struct Receipt {
    EventID event;
    uint64_t ts;
  };

  struct PendingEvent {
    QString type;
    QJsonObject content;
  };

  Room(Matrix &universe, Session &session, QString id, const QJsonObject &initial,
       lmdb::env &env, lmdb::txn &init_txn, lmdb::dbi &&member_db);

  Room(const Room &) = delete;
  Room &operator=(const Room &) = delete;

  const Session &session() const { return session_; }
  Session &session() { return session_; }
  const RoomID &id() const { return id_; }
  uint64_t highlight_count() const { return highlight_count_; }
  uint64_t notification_count() const { return notification_count_; }

  const RoomState &initial_state() const { return initial_state_; }
  const RoomState &state() const { return state_; }

  QString pretty_name() const;
  QString pretty_name_highlights() const {
    return pretty_name() + (highlight_count() != 0 ? " (" + QString::number(highlight_count()) + ")" : "");
  }

  void load_state(lmdb::txn &txn, gsl::span<const proto::Event>);
  bool dispatch(lmdb::txn &txn, const proto::JoinedRoom &);

  const std::deque<Batch> &buffer() const { return buffer_; }
  size_t buffer_size() const;

  QJsonObject to_json() const;

  enum class Direction { FORWARD, BACKWARD };

  MessageFetch *get_messages(Direction dir, QString from, uint64_t limit = 0, QString to = "");

  EventSend *leave();

  void send(const QString &type, QJsonObject content);
  void redact(const EventID &event, const QString &reason = "");

  void send_file(const QString &uri, const QString &name, const QString &media_type, size_t size);
  void send_message(const QString &body);
  void send_emote(const QString &body);

  void send_read_receipt(const EventID &event);

  bool has_unread() const;

  gsl::span<const UserID> typing() const { return typing_; }
  gsl::span<const Receipt * const> receipts_for(const EventID &id) const;
  const Receipt *receipt_from(const UserID &id) const;

  const std::deque<PendingEvent> &pending_events() const { return pending_events_; }
  // Events that have not yet been successfully transmitted

signals:
  void membership_changed(const Member &, Membership old);
  void member_disambiguation_changed(const Member &, const QString &old);
  void member_name_changed(const Member &, const QString &old); // Assume disambiguation changed as well
  void state_changed();
  void highlight_count_changed(uint64_t old);
  void notification_count_changed(uint64_t old);
  void name_changed();
  void canonical_alias_changed();
  void aliases_changed();
  void topic_changed(const QString &old);
  void avatar_changed();
  void discontinuity();
  void typing_changed();
  void receipts_changed();

  void prev_batch(const QString &);
  void message(const proto::Event &);

  void error(const QString &msg);
  void left(Membership reason);

private:
  Matrix &universe_;
  Session &session_;
  const RoomID id_;
  lmdb::env &db_env_;
  lmdb::dbi member_db_;

  RoomState initial_state_;
  std::deque<Batch> buffer_;
  RoomState state_;

  uint64_t highlight_count_ = 0, notification_count_ = 0;

  std::unordered_map<EventID, std::vector<Receipt *>, QStringHash> receipts_by_event_;
  std::unordered_map<UserID, Receipt, QStringHash> receipts_by_user_;

  std::vector<UserID> typing_;

  // State used for reliable in-order message delivery in send, transmit_event, and transmit_finished
  std::deque<PendingEvent> pending_events_;
  QNetworkReply *transmitting_;
  QTimer transmit_retry_timer_;
  std::chrono::steady_clock::duration retry_backoff_;
  QString last_transmit_transaction_;

  void update_receipt(const UserID &user, const EventID &event, uint64_t ts);

  void transmit_event();
  void transmit_finished();
};

}

#endif
