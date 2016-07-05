#include "utils.hpp"

#include <QNetworkReply>
#include <QJsonDocument>
#include <QObject>

namespace matrix {

QByteArray encode(QJsonObject o) {
  return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

Response decode(QNetworkReply *reply) {
  Response r;
  auto data = reply->readAll();
  r.code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if(r.code == 0) {
    r.error = reply->errorString();
    return r;
  }
  QJsonParseError err{0, QJsonParseError::NoError};
  auto json = QJsonDocument::fromJson(data, &err);
  if(err.error || !json.isObject()) {
    QString msg;
    msg = QObject::tr("Malformed response from server: %1").arg(err.errorString());
    if(data.size()) {
      msg += QObject::tr("\nResponse was:\n%1").arg(QString::fromUtf8(data));
    }
    r.error = msg;
    return r;
  }

  r.object = json.object();

  if(r.code != 200) {
    r.error = r.object["error"].toString();
    if(!r.error->size()) {
      r.error = QString("HTTP " + QString::number(r.code) + ": " + reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString());
    }
  }
  return r;
}

}
