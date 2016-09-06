#ifndef NATIVE_CHAT_MATRIX_ROOM_H_
#define NATIVE_CHAT_MATRIX_ROOM_H_

#include <vector>
#include <unordered_map>
#include <deque>
#include <chrono>

#include <QString>
#include <QObject>
#include <QUrl>
#include <QTimer>

#include <span.h>

#include "../QStringHash.hpp"

#include "Event.hpp"

class QNetworkReply;

namespace matrix {

class Matrix;
class Session;
class Room;

namespace proto {
struct JoinedRoom;
struct Timeline;
}

inline QString pretty_name(const UserID &user, const event::room::MemberContent profile) {
  return profile.displayname() ? *profile.displayname() : user.value();
}

using Member = std::pair<const UserID, event::room::MemberContent>;

class RoomState {
public:
  RoomState() = default;  // New, empty room
  RoomState(const QJsonObject &state, gsl::span<const Member> members);  // Loaded from db

  void apply(const event::room::State &e) {
    dispatch(e, nullptr);
  }
  void revert(const event::room::State &e);  // Reverts an event that, if a state event, has prev_content

  bool dispatch(const event::room::State &e, Room *room);
  // Returns true if changes were made. Emits state change events on room if supplied.

  const std::experimental::optional<QString> &name() const { return name_; }
  const std::experimental::optional<QString> &canonical_alias() const { return canonical_alias_; }
  gsl::span<const QString> aliases() const { return aliases_; }
  const std::experimental::optional<QString> &topic() const { return topic_; }
  const QUrl &avatar() const { return avatar_; }

  std::vector<const Member *> members() const;
  const event::room::MemberContent *member_from_id(const UserID &id) const;

  QString pretty_name(const UserID &own_id) const;
  // Matrix r0.1.0 11.2.2.5 ish (like vector-web)

  std::experimental::optional<QString> member_disambiguation(const UserID &member) const;
  std::experimental::optional<QString> nonmember_disambiguation(const UserID &id, const QString &displayname) const;
  QString member_name(const UserID &member) const;
  // Matrix r0.1.0 11.2.2.3

  QJsonObject to_json() const;
  // For serialization

private:
  std::experimental::optional<QString> name_, canonical_alias_, topic_;
  std::vector<QString> aliases_;
  QUrl avatar_;
  std::unordered_map<UserID, event::room::MemberContent> members_by_id_;
  std::unordered_map<QString, std::vector<UserID>, QStringHash> members_by_displayname_;

  void forget_displayname(const UserID &member, const QString &old_name, Room *room);
  void record_displayname(const UserID &member, const QString &name, Room *room);
  std::vector<UserID> &members_named(QString displayname);
  const std::vector<UserID> &members_named(QString displayname) const;

  bool update_membership(const UserID &user_id, const event::room::MemberContent &content, Room *room);
};

class MessageFetch : public QObject {
  Q_OBJECT

public:
  MessageFetch(QObject *parent = nullptr) : QObject(parent) {}

signals:
  void finished(const TimelineCursor &start, const TimelineCursor &end, gsl::span<const event::Room> events);
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

struct Batch {
  TimelineCursor begin;
  std::vector<event::Room> events;

  Batch(TimelineCursor begin, std::vector<event::Room> events) : begin{begin}, events{std::move(events)} {}
  Batch(const proto::Timeline &t);
  Batch(const QJsonObject &o);

  QJsonObject to_json() const;
};

class Room : public QObject {
  Q_OBJECT

public:
  struct Receipt {
    EventID event;
    uint64_t ts;
  };

  struct PendingEvent {
    TransactionID transaction_id;
    EventType type;
    event::Content content;
  };

  Room(Matrix &universe, Session &session, RoomID id, const QJsonObject &initial,
       gsl::span<const Member> members);
  Room(Matrix &universe, Session &session, const proto::JoinedRoom &joined_room);

  Room(const Room &) = delete;
  Room &operator=(const Room &) = delete;

  const Session &session() const { return session_; }
  Session &session() { return session_; }
  const RoomID &id() const { return id_; }
  uint64_t highlight_count() const { return highlight_count_; }
  uint64_t notification_count() const { return notification_count_; }

  const RoomState &state() const { return state_; }

  QString pretty_name() const;
  QString pretty_name_highlights() const {
    return pretty_name() + (highlight_count() != 0 ? " (" + QString::number(highlight_count()) + ")" : "");
  }

  bool dispatch(const proto::JoinedRoom &);

  QJsonObject to_json() const;

  MessageFetch *get_messages(Direction dir, const TimelineCursor &from, uint64_t limit = 0, std::experimental::optional<TimelineCursor> to = {});

  EventSend *leave();

  TransactionID send(const EventType &type, event::Content content);

  TransactionID redact(const EventID &event, const QString &reason = "");

  TransactionID send_file(const QString &uri, const QString &name, const QString &media_type, size_t size);
  TransactionID send_message(const QString &body);
  TransactionID send_emote(const QString &body);

  void send_read_receipt(const EventID &event);

  gsl::span<const UserID> typing() const { return typing_; }
  gsl::span<const Receipt * const> receipts_for(const EventID &id) const;
  const Receipt *receipt_from(const UserID &id) const;

  const std::deque<PendingEvent> &pending_events() const { return pending_events_; }
  // Events that have not yet been successfully transmitted

  const Batch &last_batch() const { return *last_batch_; }

signals:
  void member_changed(const UserID &, const event::room::MemberContent &old, const event::room::MemberContent &current);
  void member_disambiguation_changed(const UserID &, const std::experimental::optional<QString> &old, const std::experimental::optional<QString> &current);
  void state_changed();
  void highlight_count_changed(uint64_t old);
  void notification_count_changed(uint64_t old);
  void name_changed();
  void canonical_alias_changed();
  void aliases_changed();
  void topic_changed(const std::experimental::optional<QString> &old);
  void avatar_changed();
  void typing_changed();
  void receipts_changed();

  void batch(const proto::Timeline &);

  void prev_batch(const TimelineCursor &);
  void message(const event::Room &);

  void error(const QString &msg);
  void left(Membership reason);

private:
  Matrix &universe_;
  Session &session_;
  const RoomID id_;

  RoomState state_;
  std::experimental::optional<Batch> last_batch_;

  uint64_t highlight_count_ = 0, notification_count_ = 0;

  std::unordered_map<EventID, std::vector<Receipt *>> receipts_by_event_;
  std::unordered_map<UserID, Receipt> receipts_by_user_;

  std::vector<UserID> typing_;

  // State used for reliable in-order message delivery in send, transmit_event, and transmit_finished
  std::deque<PendingEvent> pending_events_;
  QNetworkReply *transmitting_;
  QTimer transmit_retry_timer_;
  std::chrono::steady_clock::duration retry_backoff_;

  void update_receipt(const UserID &user, const EventID &event, uint64_t ts);

  void transmit_event();
  void transmit_finished();
};

}

#endif
