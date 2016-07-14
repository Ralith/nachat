#include "proto.hpp"

namespace matrix {
namespace proto {

QJsonObject to_json(const Unsigned &u) {
  QJsonObject o;
  if(u.prev_content) o["prev_content"] = *u.prev_content;
  o["age"] = static_cast<qint64>(u.age);
  if(u.transaction_id) o["transaction_id"] = *u.transaction_id;
  return o;
}

QJsonObject to_json(const Event &e) {
  QJsonObject o;
  o["content"] = e.content;
  o["origin_server_ts"] = static_cast<qint64>(e.origin_server_ts);
  o["sender"] = e.sender;
  o["type"] = e.type;
  o["unsigned"] = to_json(e.unsigned_);
  o["state_key"] = e.state_key;
  return o;
}

}
}
