#include "Event.hpp"

#include <utility>

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

Event::Event(QJsonObject o) : json_(std::move(o)) {
  check(json(), {
      {"content", QJsonValue::Object},
      {"type", QJsonValue::String}
    });
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
  if(redacted()) return;
  check(json(), {
      {"sender", QJsonValue::String},
      {"origin_server_ts", QJsonValue::Double},
      {"unsigned", QJsonValue::Object, false}
    });
}

namespace room {

Message::Message(Room e) : Room(std::move(e)) {
  if(type() != tag()) throw type_mismatch();
  if(redacted()) return;
  check(content().json(), {
      {"msgtype", "content.msgtype", QJsonValue::String},
      {"body", "content.body", QJsonValue::String}
    });
}

namespace message {

FileLike::FileLike(Message m) : Message(std::move(m)) {
  if(redacted()) return;
  check(content().json(), {
      {"url", "content.url", QJsonValue::String},
    });
  QJsonObject i;
  {
    auto it = json().find("info");
    if(it == json().end() || it->isNull()) return;
    i = it->toObject();
  }
  {
    auto it = i.find("mimetype");
    if(it != i.end() && !it->isNull())
      throw ill_typed_field("content.info.mimetype", QJsonValue::String, it->type());
  }
  {
    auto it = i.find("size");
    if(it != i.end() && !it->isDouble() && !it->isNull())
      throw ill_typed_field("content.info.size", QJsonValue::Double, it->type());
  }
}

File::File(FileLike m) : FileLike(std::move(m)) {
  if(msgtype() != tag()) throw type_mismatch();
  if(redacted()) return;
  check(content().json(), {
      {"filename", "content.filename", QJsonValue::String},
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
    if(it != json().end() && !it->isNull())
      avatar_url_ = json()["avatar_url"].toString();
  }
  {
    auto it = json().find("displayname");
    if(it != json().end() && !it->isNull())
      displayname_ = json()["displayname"].toString();
  }
}

const MemberContent MemberContent::leave(Content({{"membership", "leave"}}));

Member::Member(State e) : State(std::move(e)), content_{State::content()} {
  if(type() != tag()) throw type_mismatch();
  auto prev = State::prev_content();
  if(prev) {
    prev_content_ = MemberContent(*prev);
  }
}

Name::Name(State e) : State(std::move(e)) {
  if(redacted()) return;
  check(content().json(), {{"name", "content.name", QJsonValue::String}});
}

Aliases::Aliases(State e) : State(std::move(e)) {
  check(content().json(), {{"aliases", "content.aliases", QJsonValue::Array}});
}

CanonicalAlias::CanonicalAlias(State e) : State(std::move(e)) {
  if(redacted()) return;
  check(content().json(), {{"alias", "content.alias", QJsonValue::String}});
}

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

}

}
}
