#ifndef NATIVE_CHAT_MATRIX_UTILS_HPP_
#define NATIVE_CHAT_MATRIX_UTILS_HPP_

#include <experimental/optional>

#include <QByteArray>
#include <QJsonObject>

class QNetworkReply;

namespace matrix {

QByteArray encode(QJsonObject o);

struct Response {
  int code;
  QJsonObject object;
  std::experimental::optional<QString> error;
};

Response decode(QNetworkReply *reply);

}

#endif
