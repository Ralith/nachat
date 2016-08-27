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
  if(err.error) {
    if(r.code >= 300) {
      // If we couldn't parse the json returned with an error, we probably aren't talking to a matrix server, so just return the HTTP code.
      r.error = QObject::tr("HTTP %1 %2").arg(reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString());
      return r;
    }

    QString msg;
    msg = QObject::tr("Malformed response from server: %1").arg(err.errorString());
    if(data.size()) {
      msg += QObject::tr("\nResponse was:\n%1").arg(QString::fromUtf8(data));
    }
    r.error = msg;
    return r;
  }

  if(!json.isObject()) {
    r.error = QObject::tr("Malformed response from server: not a json object\nResponse was:\n%1").arg(QString::fromUtf8(data));
    return r;
  }

  r.object = json.object();

  if(r.code >= 300) {
    r.error = r.object["error"].toString();
    if(!r.error->size()) {
      r.error = QObject::tr("HTTP %1 %2").arg(reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString());
    }
  }
  return r;
}

}
