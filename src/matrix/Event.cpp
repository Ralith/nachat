#include "Event.hpp"

#include <utility>

#include <initializer_list>

namespace matrix {

static Membership parse_membership(const QString &m) {
  static const std::pair<QString, Membership> table[] = {
    {"invite", Membership::INVITE},
    {"join", Membership::JOIN},
    {"leave", Membership::LEAVE},
    {"ban", Membership::BAN}
  };
  for(const auto &x : table) {
    if(x.first == m) return x.second;
  }
  throw malformed_event("unrecognized membership value");
}

static QString to_qstring(Membership m) {
  switch(m) {
  case Membership::INVITE: return "invite";
  case Membership::JOIN: return "join";
  case Membership::LEAVE: return "leave";
  case Membership::BAN: return "ban";
  }
}

struct EventInfo {
  const char *real_name;
  const char *name;
  QJsonValue::Type type;
  bool required;

  EventInfo(const char *name, QJsonValue::Type type, bool required = true) : real_name(name), name(name), type(type), required(required) {}
  EventInfo(const char *real_name, const char *name, QJsonValue::Type type, bool required = true) : real_name(real_name), name(name), type(type), required(required) {}
};

static void check(const QJsonObject &o, std::initializer_list<EventInfo> fields) {
  for(auto field : fields) {
    auto it = o.find(field.real_name);
    if(it != o.end()) {
      if((field.required || !it->isNull()) && it->type() != field.type) {
        throw ill_typed_field(field.name, field.type, it->type());
      }
    } else if(field.required) {
      throw missing_field(field.name);
    }
  }
}

namespace event {

UnsignedData::UnsignedData(QJsonObject o) : json_{std::move(o)} {
  check(json(), {
      {"age", "unsigned.age", QJsonValue::Double, false},
      {"redacted_because", "unsigned.redacted_because", QJsonValue::Object, false}
    });

  auto it = json().find("redacted_because");
  if(it != json().end()) {
    redacted_because_ = std::make_shared<event::room::Redaction>(event::Room{event::Identifiable{Event{it->toObject()}}});
  }
}

}

Event::Event(QJsonObject o) : json_(std::move(o)) {
  check(json(), {
      {"content", QJsonValue::Object},
      {"type", QJsonValue::String},
      {"unsigned", QJsonValue::Object, false}
    });

  auto it = json().find("unsigned");
  if(it != json().end()) {
    unsigned_data_ = event::UnsignedData{it->toObject()};
  }
}

void Event::redact(const event::room::Redaction &because) {
  struct ContentRule {
    EventType type;
    std::initializer_list<const char *> preserved_keys;
  };

  using namespace event::room;

  // section 6.5
  const char *const preserved_keys[] = {"event_id", "type", "room_id", "sender", "state_key", "prev_content", "content"};
  const ContentRule content_rules[] = {
    {Member::tag(), {"membership"}},
    {Create::tag(), {"creator"}},
    {JoinRules::tag(), {"join_rule"}},
    {PowerLevels::tag(), {"ban", "events", "events_default", "kick", "redact", "state_default", "users", "users_default"}},
    {Aliases::tag(), {"aliases"}},
  };

  for(auto it = json_.begin(); it != json_.end();) {
    auto p = std::find(std::begin(preserved_keys), std::end(preserved_keys), it.key());
    if(p == std::end(preserved_keys)) {
      it = json_.erase(it);
    } else {
      ++it;
    }
  }

  auto content_rule = std::find_if(std::begin(content_rules), std::end(content_rules), [this](const ContentRule &c) {
      return c.type == type();
    });
  if(content_rule != std::end(content_rules)) {
    const auto &keys = content_rule->preserved_keys;
    auto content_obj = json_.take("content").toObject();
    for(auto it = content_obj.begin(); it != content_obj.end();) {
      auto p = std::find(std::begin(keys), std::end(keys), it.key());
      if(p == std::end(keys)) {
        it = content_obj.erase(it);
      } else {
        ++it;
      }
    }
    json_.insert("content", content_obj);
  }

  auto unsigned_it = json_.insert("unsigned", QJsonObject{{"redacted_because", because.json()}});
  unsigned_data_ = event::UnsignedData{unsigned_it->toObject()};
}

namespace event {

Receipt::Receipt(Event e) : Event(std::move(e)) {
  if(type() != tag()) throw type_mismatch();
}

Typing::Typing(Event e) : Event(std::move(e)) {
  if(type() != tag()) throw type_mismatch();
  check(content().json(), {
      {"user_ids", "content.user_ids", QJsonValue::Array}
    });
}

std::vector<UserID> Typing::user_ids() const {
  std::vector<UserID> result;
  const auto ids = content().json()["user_ids"].toArray();
  result.reserve(ids.size());
  for(const auto &x : ids) {
    result.push_back(UserID(x.toString()));
  }
  return result;
}

Identifiable::Identifiable(Event e) : Event(e) {
  check(json(), {
      {"event_id", QJsonValue::String}
    });
}

Room::Room(Identifiable e) : Identifiable(std::move(e)) {
  check(json(), {{"sender", QJsonValue::String}});

  if(redacted()) return;

  check(json(), {
      {"origin_server_ts", QJsonValue::Double},
      {"unsigned", QJsonValue::Object, false}
    });
}

namespace room {

MessageContent::MessageContent(Content c) : Content(std::move(c)) {
  check(json(), {
      {"msgtype", "content.msgtype", QJsonValue::String},
      {"body", "content.body", QJsonValue::String}
    });
}

Message::Message(Room e) : Room(std::move(e)) {
  if(type() != tag()) throw type_mismatch();
  if(redacted()) return;
  content_ = MessageContent(Event::content());
}

namespace message {

FileLike::FileLike(MessageContent m) : MessageContent(std::move(m)) {
  check(json(), {
      {"url", QJsonValue::String},
    });
  QJsonObject i;
  {
    auto it = json().find("info");
    if(it == json().end() || it->isNull()) return;
    i = it->toObject();
  }
  {
    auto it = i.find("mimetype");
    if(it != i.end() && !it->isString() && !it->isNull())
      throw ill_typed_field("info.mimetype", QJsonValue::String, it->type());
  }
  {
    auto it = i.find("size");
    if(it != i.end() && !it->isDouble() && !it->isNull())
      throw ill_typed_field("info.size", QJsonValue::Double, it->type());
  }
}

File::File(FileLike m) : FileLike(std::move(m)) {
  if(type() != tag()) throw type_mismatch();
  check(json(), {
      {"filename", QJsonValue::String}
    });
}

}

State::State(Room e) : Room(std::move(e)) {
  check(json(), {
      {"state_key", QJsonValue::String}
    });
}

MemberContent::MemberContent(Content c) : Content(std::move(c)) {
  check(json(), {
      {"membership", "content.membership", QJsonValue::String},
      {"avatar_url", "content.avatar_url", QJsonValue::String, false},
      {"displayname", "content.displayname", QJsonValue::String, false}
    });
  membership_ = parse_membership(json()["membership"].toString());
  {
    auto it = json().find("avatar_url");
    if(it != json().end() && !it->isNull() && it->toString() != "")
      avatar_url_ = json()["avatar_url"].toString();
  }
  {
    auto it = json().find("displayname");
    if(it != json().end() && !it->isNull() && it->toString() != "")
      displayname_ = json()["displayname"].toString();
  }
}

MemberContent::MemberContent(Membership membership,
                             std::experimental::optional<QString> displayname,
                             std::experimental::optional<QString> avatar_url)
  : Content({
      {"membership", to_qstring(membership)},
      {"displayname", displayname ? QJsonValue(*displayname) : QJsonValue()},
      {"avatar_url", avatar_url ? QJsonValue(*avatar_url) : QJsonValue()}
    }),
    membership_{membership}, avatar_url_{avatar_url}, displayname_{displayname} {}

const MemberContent MemberContent::leave(Content({{"membership", "leave"}}));

Member::Member(State e) : State(std::move(e)), content_{State::content()} {
  if(type() != tag()) throw type_mismatch();
  auto prev = State::prev_content();
  if(prev) {
    prev_content_ = MemberContent(*prev);
  }
}

void Member::redact(const event::room::Redaction &because) {
  Event::redact(because);
  content_ = MemberContent{State::content()};
}

Aliases::Aliases(State e) : State(std::move(e)) {
  check(content().json(), {{"aliases", "content.aliases", QJsonValue::Array}});
}

CanonicalAlias::CanonicalAlias(State e) : State(std::move(e)) {}

Topic::Topic(State e) : State(std::move(e)) {
  if(redacted()) return;
  check(content().json(), {{"topic", "content.topic", QJsonValue::String}});
}

Avatar::Avatar(State e) : State(std::move(e)) {
  if(redacted()) return;
  check(content().json(), {{"url", "content.url", QJsonValue::String}});
}

Create::Create(State e) : State(std::move(e)) {
  check(content().json(), {{"creator", "content.create", QJsonValue::String}});
}

JoinRules::JoinRules(State e) : State(std::move(e)) {}

PowerLevels::PowerLevels(State e) : State(std::move(e)) {}

Redaction::Redaction(Room r) : Room(std::move(r)) {
  if(redacted()) return;
  check(json(), {{"redacts", QJsonValue::String}});
  check(content().json(), {
      {"reason", "content.reason", QJsonValue::String, false}
    });
}

}

}
}
