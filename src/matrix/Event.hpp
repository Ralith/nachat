#ifndef NATIVE_CLIENT_MATRIX_EVENT_HPP_
#define NATIVE_CLIENT_MATRIX_EVENT_HPP_

#include <stdexcept>
#include <experimental/optional>
#include <vector>
#include <memory>

#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>

#include "ID.hpp"

namespace matrix {

class type_mismatch : public std::logic_error {
public:
  type_mismatch() : std::logic_error::logic_error("event has incorrect \"type\" field") {}
};

class malformed_event : public std::invalid_argument {
public:
  using std::invalid_argument::invalid_argument;
};

class missing_field : public malformed_event {
public:
  missing_field(const char *field) : malformed_event("event missing required field"), field_(field) {}

  const char *field() const noexcept { return field_; }

private:
  const char *field_;
};

class ill_typed_field : public malformed_event {
public:
  ill_typed_field(const char *field, QJsonValue::Type expected, QJsonValue::Type actual)
    : malformed_event("event field had wrong type"), field_{field}, expected_{expected}, actual_{actual} {}

  const char *field() const noexcept { return field_; }
  QJsonValue::Type expected() const noexcept { return expected_; }
  QJsonValue::Type actual() const noexcept { return actual_; }

private:
  const char *field_;
  QJsonValue::Type expected_;
  QJsonValue::Type actual_;
};

enum class Membership {
  INVITE, JOIN, LEAVE, BAN
};

// Whether a membership participates in naming per 11.2.2.3
constexpr inline bool membership_displayable(Membership m) {
  return m == Membership::JOIN || m == Membership::INVITE;
}

namespace event {

class Content {
public:
  explicit Content(QJsonObject o) : json_(std::move(o)) {}

  const QJsonObject &json() const noexcept { return json_; }

private:
  QJsonObject json_;
};

namespace room {
class Redaction;
}

class UnsignedData {
public:
  explicit UnsignedData(QJsonObject o);

  const QJsonObject &json() const noexcept { return json_; }

  std::experimental::optional<int64_t> age() const noexcept {
    auto it = json().find("age");
    if(it != json().end()) return it->toDouble();
    return {};
  }

  std::experimental::optional<TransactionID> transaction_id() const noexcept {
    auto it = json().find("transaction_id");
    if(it != json().end()) return TransactionID{it->toString()};
    return {};
  }

  std::experimental::optional<event::room::Redaction> redacted_because() const noexcept;
  bool redacted() const {
    return static_cast<bool>(redacted_because_);
  }

private:
  QJsonObject json_;
  std::shared_ptr<event::room::Redaction> redacted_because_;
};

}

class Event {
public:
  explicit Event(QJsonObject);

  const QJsonObject &json() const noexcept { return json_; }

  event::Content content() const noexcept { return event::Content(json()["content"].toObject()); }
  EventType type() const noexcept { return EventType(json()["type"].toString()); }
  const std::experimental::optional<event::UnsignedData> &unsigned_data() const noexcept { return unsigned_data_; }  

  virtual void redact(const event::room::Redaction &because);

  bool redacted() const { return unsigned_data_ && unsigned_data_->redacted(); }

private:
  QJsonObject json_;
  std::experimental::optional<event::UnsignedData> unsigned_data_;
};

namespace event {

class Receipt : public Event {
public:
  explicit Receipt(Event);

  static EventType tag() { return EventType("m.receipt"); }
};

class Typing : public Event {
public:
  explicit Typing(Event);

  std::vector<UserID> user_ids() const;

  static EventType tag() { return EventType("m.typing"); }
};

class Identifiable : public Event {
public:
  explicit Identifiable(Event);

  EventID id() const noexcept { return EventID(json()["event_id"].toString()); }
};

namespace room {
class State;
}

class Room : public Identifiable {
public:
  explicit Room(Identifiable);

  UserID sender() const noexcept { return UserID(json()["sender"].toString()); }
  uint64_t origin_server_ts() const noexcept { return json()["origin_server_ts"].toDouble(); }
  std::experimental::optional<room::State> to_state() const noexcept;
};

namespace room {

class MessageContent : public Content {
public:
  MessageContent() : Content(QJsonObject{}) {}
  explicit MessageContent(Content);

  QString body() const noexcept { return json()["body"].toString(); }

  MessageType type() const noexcept { return MessageType(json()["msgtype"].toString()); }
};

class Message : public Room {
public:
  explicit Message(Room);

  static EventType tag() { return EventType("m.room.message"); }

  const MessageContent &content() const { return content_; }

private:
  MessageContent content_;
};

namespace message {

class Text : public MessageContent {
public:
  explicit Text(MessageContent m) : MessageContent(std::move(m)) { if(type() != tag()) throw type_mismatch(); }

  static MessageType tag() { return MessageType("m.text"); }
};

class Emote : public MessageContent {
public:
  explicit Emote(MessageContent m) : MessageContent(std::move(m)) { if(type() != tag()) throw type_mismatch(); }

  static MessageType tag() { return MessageType("m.emote"); }
};

class Notice : public MessageContent {
public:
  explicit Notice(MessageContent m) : MessageContent(std::move(m)) { if(type() != tag()) throw type_mismatch(); }

  static MessageType tag() { return MessageType("m.notice"); }
};

class FileLike : public MessageContent {
public:
  explicit FileLike(MessageContent m);

  QJsonObject info() const { return json()["info"].toObject(); }
  std::experimental::optional<QString> mimetype() const {
    auto i = info();
    auto it = i.find("mimetype");
    if(it != i.end() && !it->isNull()) return it->toString();
    return {};
  }
  std::experimental::optional<size_t> size() const {
    auto i = info();
    auto it = i.find("size");
    if(it != i.end() && !it->isNull()) return it->toDouble();
    return {};
  }
  QString url() const { return json()["url"].toString(); }
};

class File : public FileLike {
public:
  explicit File(FileLike m);

  QString filename() const { return json()["filename"].toString(); }

  static MessageType tag() { return MessageType("m.file"); }
};

class Image : public FileLike {
public:
  explicit Image(FileLike m) : FileLike(std::move(m)) { if(type() != tag()) throw type_mismatch(); }

  static MessageType tag() { return MessageType("m.image"); }
};

class Video : public FileLike {
public:
  explicit Video(FileLike m) : FileLike(std::move(m)) { if(type() != tag()) throw type_mismatch(); }

  static MessageType tag() { return MessageType("m.video"); }
};

class Audio : public FileLike {
public:
  explicit Audio(FileLike m) : FileLike(std::move(m)) { if(type() != tag()) throw type_mismatch(); }

  static MessageType tag() { return MessageType("m.audio"); }
};

}

class State : public Room {
public:
  explicit State(Room);

  QString state_key() const noexcept { return json()["state_key"].toString(); }
  std::experimental::optional<Content> prev_content() const noexcept {
    auto u = unsigned_data();
    if(!u) return {};
    auto it = u->json().find("prev_content");
    if(it == u->json().end() || it->isNull()) return {};
    return Content(it->toObject());
  }
};

class MemberContent : public Content {
public:
  explicit MemberContent(Content);
  MemberContent(Membership membership,
                std::experimental::optional<QString> displayname,
                std::experimental::optional<QString> avatar_url);

  static const MemberContent leave;

  Membership membership() const { return membership_; }
  const std::experimental::optional<QString> &avatar_url() const { return avatar_url_; }
  const std::experimental::optional<QString> &displayname() const { return displayname_; }

private:
  Membership membership_;
  std::experimental::optional<QString> avatar_url_, displayname_;
};

class Member : public State {
public:
  explicit Member(State);

  static EventType tag() { return EventType("m.room.member"); }

  UserID user() const noexcept { return UserID(state_key()); }
  const MemberContent &content() const { return content_; }
  const std::experimental::optional<MemberContent> &prev_content() const noexcept { return prev_content_; }

  void redact(const event::room::Redaction &because) override;

private:
  MemberContent content_;
  std::experimental::optional<MemberContent> prev_content_;
};

class NameContent : public Content {
public:
  explicit NameContent(Content c) : Content{std::move(c)} {}

  std::experimental::optional<QString> name() const noexcept {
    auto it = json().find("name");
    if(it == json().end() || it->isNull() || it->toString() == "") return {};
    return it->toString();
  }
};

class Name : public State {
public:
  explicit Name(State s) : State(std::move(s)) {}

  NameContent content() const noexcept { return NameContent(State::content()); }
  std::experimental::optional<NameContent> prev_content() const noexcept {
    if(auto c = State::prev_content()) {
      return NameContent(*c);
    }
    return {};
  }

  static EventType tag() { return EventType("m.room.name"); }
};

class Aliases : public State {
public:
  explicit Aliases(State);
  
  static EventType tag() { return EventType("m.room.aliases"); }

  QJsonArray aliases() const { return content().json()["aliases"].toArray(); }
  std::experimental::optional<QJsonArray> prev_aliases() const noexcept {
    if(auto c = prev_content()) return c->json()["aliases"].toArray();
    return {};
  }
};

class CanonicalAlias : public State {
public:
  explicit CanonicalAlias(State);

  std::experimental::optional<QString> alias() const noexcept {
    auto c = content();
    auto it = c.json().find("alias");
    if(it == c.json().end() || it->isNull() || it->toString() == "") return {};
    return it->toString();
  }
  std::experimental::optional<QString> prev_alias() const noexcept {
    if(auto c = prev_content()) {
      auto it = c->json().find("alias");
      if(it == c->json().end() || it->isNull() || it->toString() == "") return {};
      return it->toString();
    }
    return {};
  }

  static EventType tag() { return EventType("m.room.canonical_alias"); }
};

class Topic : public State {
public:
  explicit Topic(State);

  QString topic() const noexcept { return content().json()["topic"].toString(); }
  std::experimental::optional<QString> prev_topic() const noexcept {
    if(auto c = prev_content()) return c->json()["topic"].toString();
    return {};
  }

  static EventType tag() { return EventType("m.room.topic"); }
};

class Avatar : public State {
public:
  explicit Avatar(State);

  QString avatar() const noexcept { return content().json()["url"].toString(); }
  std::experimental::optional<QString> prev_avatar() const noexcept {
    if(auto c = prev_content()) return c->json()["url"].toString();
    return {};
  }

  static EventType tag() { return EventType("m.room.avatar"); }
};

class Create : public State {
public:
  explicit Create(State);

  UserID creator() const noexcept { return UserID(content().json()["creator"].toString()); }
  std::experimental::optional<UserID> prev_creator() const noexcept {
    if(auto c = prev_content()) return UserID(c->json()["creator"].toString());
    return {};
  }

  static EventType tag() { return EventType("m.room.create"); }
};

class JoinRules : public State {
public:
  explicit JoinRules(State);

  static EventType tag() { return EventType("m.room.join_rules"); }
};

class PowerLevels : public State {
public:
  explicit PowerLevels(State);

  static EventType tag() { return EventType("m.room.power_levels"); }
};


class RedactionContent : public Content {
public:
  explicit RedactionContent(Content c) : Content{std::move(c)} {}

  std::experimental::optional<QString> reason() const noexcept {
    auto it = json().find("reason");
    if(it == json().end() || it->isNull() || it->toString() == "") return {};
    return it->toString();
  }
};

class Redaction : public Room {
public:
  explicit Redaction(Room);

  EventID redacts() const noexcept { return EventID{json()["redacts"].toString()}; }
  RedactionContent content() const noexcept { return RedactionContent(Room::content()); }

  static EventType tag() { return EventType("m.room.redaction"); }
};

}

inline std::experimental::optional<room::State> Room::to_state() const noexcept {
  if(json().contains("state_key")) return room::State(*this);
  return {};
}

inline std::experimental::optional<event::room::Redaction> UnsignedData::redacted_because() const noexcept {
  if(redacted_because_) return *redacted_because_;
  return {};
}

}

}

#endif
