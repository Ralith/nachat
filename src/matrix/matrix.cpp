#include "matrix.hpp"

#include <experimental/optional>

#include <QJsonDocument>
#include <QTimer>

#include "parse.hpp"

using std::experimental::optional;

namespace matrix {

static QByteArray encode(QJsonObject o) {
  return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

struct Response {
  int code;
  QJsonObject object;
  optional<QString> error;
};

static Response decode(QNetworkReply *reply) {
  Response r;
  auto data = reply->readAll();
  printf("got %d bytes\n", data.size());
  r.code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if(r.code == 0) {
    r.error = reply->errorString();
    return r;
  }
  QJsonParseError err{0, QJsonParseError::NoError};
  auto json = QJsonDocument::fromJson(data, &err);
  if(err.error || !json.isObject()) {
    QString msg;
    msg = QObject::tr("Malformed response from server: ") % err.errorString();
    if(data.size()) {
      msg += QObject::tr("\nResponse was:\n") % QString::fromUtf8(data);
    }
    r.error = msg;
    return r;
  }

  r.object = json.object();

  if(r.code != 200) {
    r.error = r.object["error"].toString();
    if(!r.error->size()) {
      r.error = QString("HTTP " % QString::number(r.code) % ": " % reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString());
    }
  }
  return r;
}

Session::Session(Matrix& universe, QUrl homeserver, QString user_id, QString access_token)
    : universe_(universe), homeserver_(homeserver), user_id_(user_id), access_token_(access_token), synced_(false) {
  QUrlQuery query;
  query.addQueryItem("filter", encode({
        {"room", QJsonObject{
            {"timeline", QJsonObject{
                {"limit", 50}
              }},
          }}
      }));
  query.addQueryItem("full_state", "true");
  sync(query);
}

void Session::sync(QUrlQuery query) {
  auto reply = universe_.net.get(request("client/r0/sync", query));
  connect(reply, &QNetworkReply::finished, [this, reply](){
      handle_sync_reply(reply);
    });
  connect(reply, &QNetworkReply::downloadProgress, this, &Session::sync_progress);
}

void Session::handle_sync_reply(QNetworkReply *reply) {
  using namespace std::chrono_literals;

  auto r = decode(reply);
  bool was_synced = synced_;
  if(r.error) {
    synced_ = false;
    error(*r.error);
  } else {
    synced_ = true;
    auto s = parse_sync(r.object);
    next_batch_ = s.next_batch;
    dispatch(std::move(s));
  }
  if(was_synced != synced_) synced_changed();

  QUrlQuery query;
  query.addQueryItem("since", next_batch_);
  query.addQueryItem("timeout", "50000");

  auto now = std::chrono::steady_clock::now();
  constexpr std::chrono::steady_clock::duration RETRY_INTERVAL = 10s;
  auto since_last_error = now - last_sync_error_;
  if(r.error && (since_last_error < RETRY_INTERVAL)) {
    QTimer::singleShot(std::chrono::duration_cast<std::chrono::milliseconds>(RETRY_INTERVAL - since_last_error).count(),
                       [this, query](){
                         sync(query);
                       });
  } else {
    sync(query);
  }

  if(r.error) {
    last_sync_error_ = now;
  }
}

void Session::dispatch(proto::Sync sync) {
  bool joined_rooms = false;
  for(auto &joined : sync.rooms.join) {
    auto key = joined.id.toStdString();
    auto it = rooms_.find(key);
    if(it == rooms_.end()) {
      it = rooms_.emplace(std::piecewise_construct,
                          std::forward_as_tuple(key),
                          std::forward_as_tuple(universe_, user_id_, joined.id)).first;
      joined_rooms = true;
    }
    it->second.dispatch(joined);
  }
  if(joined_rooms) rooms_changed();
}

void Session::log_out() {
  auto reply = universe_.net.post(request("client/r0/logout"), encode({}));
  connect(reply, &QNetworkReply::finished, [this, reply](){
      auto r = decode(reply);
      if(!r.error || r.code == 404) {  // 404 = already logged out
        logged_out();
      } else {
        error(*r.error);
      }
    });
}

std::vector<Room *> Session::rooms() {
  std::vector<Room *> result;
  result.reserve(rooms_.size());
  std::transform(rooms_.begin(), rooms_.end(), std::back_inserter(result),
                 [](auto &x) { return &x.second; });
  return result;
}

QNetworkRequest Session::request(QString path, QUrlQuery query) {
  QUrl url(homeserver_);
  url.setPath("/_matrix/" % path);
  query.addQueryItem("access_token", access_token_);
  url.setQuery(query);
  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  req.setRawHeader("Accept", "application/json");
  return req;
}

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
      logged_in(new Session(*this, homeserver, user_id.toString(), token.toString()));
    });
}

}
