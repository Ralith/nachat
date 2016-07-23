#ifndef NATIVE_CLIENT_MATRIX_EVENT_HPP_
#define NATIVE_CLIENT_MATRIX_EVENT_HPP_

#include <QString>
#include <QJsonObject>

#include <experimental/optional>

namespace matrix {

namespace proto {

struct Unsigned {
  std::experimental::optional<QJsonObject> prev_content;
  uint64_t age;
  std::experimental::optional<QString> transaction_id;
};

struct Event {
  QJsonObject content;
  uint64_t origin_server_ts;
  QString sender;
  QString type;
  Unsigned unsigned_;
  QString state_key;
  QString event_id;
};

QJsonObject to_json(const Unsigned &);
QJsonObject to_json(const Event &);

}

}

#endif
