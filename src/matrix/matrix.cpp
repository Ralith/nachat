#include "matrix.hpp"

#include <experimental/optional>
#include <unordered_set>

#include <QJsonDocument>

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

Session::Session(QNetworkAccessManager& net, QUrl homeserver, QString access_token)
    : net_(net), homeserver_(homeserver), access_token_(access_token), synced_(false) {
  QUrlQuery query;
  query.addQueryItem("filter", encode({
        {"room", QJsonObject{
            {"timeline", QJsonObject{
                {"limit", 0}
              }},
            {"state", QJsonObject{
                {"types", QJsonArray{"m.room.name", "m.room.aliases"}}}
            }
          }}
      }));
  query.addQueryItem("full_state", "true");
  auto reply = net_.get(request("client/r0/sync", query));
  connect(reply, &QNetworkReply::finished, [this, reply](){
      handle_sync_reply(reply);
    });
}

void Session::handle_sync_reply(QNetworkReply *reply) {
  auto r = decode(reply);
  bool was_synced = synced_;
  if(r.error) {
    synced_ = false;
    error(*r.error);
  } else {
    synced_ = true;
    auto s = parse_sync(r.object);
    next_batch_ = s.next_batch;
    process_sync(std::move(s));
  }

  QUrlQuery query;
  query.addQueryItem("since", next_batch_);
  query.addQueryItem("timeout", "50000");

  if(was_synced != synced_) synced_changed();
  auto next_reply = net_.get(request("client/r0/sync", query));
  connect(next_reply, &QNetworkReply::finished, [this, next_reply](){ handle_sync_reply(next_reply); });
}

void Session::process_sync(proto::Sync sync) {
  bool joined_rooms = false;
  for(auto &joined : sync.rooms.join) {
    auto key = joined.id.toStdString();
    auto it = rooms_.find(key);
    if(it == rooms_.end()) {
      it = rooms_.emplace(std::piecewise_construct,
                          std::forward_as_tuple(key),
                          std::forward_as_tuple(joined.id)).first;
      joined_rooms = true;
    }
    auto &room = it->second;
    std::unordered_set<std::string> aliases;
    for(auto &state : joined.state.events) {
      if(state.type == "m.room.aliases") {
        auto data = state.content["aliases"].toArray();
        aliases.reserve(aliases.size() + data.size());
        std::transform(data.begin(), data.end(), std::inserter(aliases, aliases.end()),
                       [](const QJsonValue &v){ return v.toString().toStdString(); });
      } else if(state.type == "m.room.name") {
        room.name_ = state.content["name"].toString();
        room.name_changed();
      } else {
        qDebug() << tr("Unrecognized message type: ") << state.type;
      }
    }
    if(!aliases.empty()) {
      std::transform(room.aliases_.begin(), room.aliases_.end(), std::inserter(aliases, aliases.end()),
                     [](QString &s) { return std::move(s).toStdString(); });
      room.aliases_.clear();
      room.aliases_.reserve(aliases.size());
      std::transform(aliases.begin(), aliases.end(), std::back_inserter(room.aliases_),
                     [](const std::string &s) { return QString::fromStdString(s); });
      room.aliases_changed();
    }
  }
  if(joined_rooms) rooms_changed();
}

void Session::log_out() {
  auto reply = net_.post(request("client/r0/logout"), encode({}));
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

Matrix::Matrix(QNetworkAccessManager &net, QObject *parent) : QObject(parent), net_(net) {}

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

  auto reply = net_.post(request, encode(body));

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
      if(!token.isString()) {
        login_error(tr("Malformed response from server"));
        return;
      }
      logged_in(new Session(net_, homeserver, token.toString()));
    });
}

}
