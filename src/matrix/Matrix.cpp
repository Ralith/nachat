#include "Matrix.hpp"

#include <QtNetwork>
#include <QJsonObject>
#include <QStandardPaths>

#include "utils.hpp"
#include "Event.hpp"

namespace matrix {

Matrix::Matrix(QNetworkAccessManager &net, QObject *parent) : QObject(parent), net(net) {}

void Matrix::login(QUrl homeserver, QString username, QString password) {
  QUrl login_url(homeserver);
  login_url.setPath("/_matrix/client/r0/login");
  QNetworkRequest request(login_url);
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  QJsonObject body{
      {"type", "m.login.password"},
      {"user", username},
      {"password", password}
    };

  auto reply = net.post(request, encode(body));

  connect(reply, &QNetworkReply::finished, [this, reply, homeserver](){
      auto r = decode(reply);
      if(r.code == 403) {
        login_error(tr("Login failed. Check username/password."));
        return;
      }
      if(r.error) {
        login_error(*r.error);
        return;
      }
      auto token = r.object["access_token"];
      auto user_id = r.object["user_id"];
      if(!token.isString() || !user_id.isString()) {
        login_error(tr("Malformed response from server"));
        return;
      }
      logged_in(UserID(user_id.toString()), token.toString());
    });
}

}
