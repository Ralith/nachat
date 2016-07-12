#include "Session.hpp"

#include <stdexcept>

#include <QTimer>

#include "utils.hpp"
#include "matrix.hpp"
#include "parse.hpp"

namespace matrix {

Session::Session(Matrix& universe, QUrl homeserver, QString user_id, QString access_token)
    : universe_(universe), homeserver_(homeserver), user_id_(user_id), buffer_size_(50),
      access_token_(access_token), synced_(false) {
  QUrlQuery query;
  query.addQueryItem("filter", encode({
        {"room", QJsonObject{
            {"timeline", QJsonObject{
                {"limit", static_cast<int>(buffer_size_)}
              }},
          }}
      }));
  query.addQueryItem("full_state", "true");
  sync(query);
}

void Session::sync(QUrlQuery query) {
  auto reply = get("client/r0/sync", query);
  connect(reply, &QNetworkReply::finished, [this, reply](){
      reply->deleteLater();
      sync_progress(0, 0);
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
  for(auto &joined_room : sync.rooms.join) {
    auto it = rooms_.find(joined_room.id);
    bool new_room = false;
    if(it == rooms_.end()) {
      it = rooms_.emplace(std::piecewise_construct,
                          std::forward_as_tuple(joined_room.id),
                          std::forward_as_tuple(universe_, *this, joined_room.id)).first;
      new_room = true;
    }
    auto &room = it->second;
    room.load_state(joined_room.state.events);
    if(new_room) joined(room);
    room.dispatch(joined_room);
  }
  sync_complete();
}

void Session::log_out() {
  auto reply = post("client/r0/logout", {});
  connect(reply, &QNetworkReply::finished, [this, reply](){
      reply->deleteLater();
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

QNetworkRequest Session::request(const QString &path, QUrlQuery query) {
  QUrl url(homeserver_);
  url.setPath("/_matrix/" % path);
  query.addQueryItem("access_token", access_token_);
  url.setQuery(query);
  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  req.setRawHeader("Accept", "application/json");
  return req;
}

QNetworkReply *Session::get(const QString &path, QUrlQuery query) {
  return universe_.net.get(request(path, query));
}

QNetworkReply *Session::post(const QString &path, QJsonObject body, QUrlQuery query) {
  return universe_.net.post(request(path, query), encode(body));
}

QNetworkReply *Session::post(const QString &path, QIODevice *data, QUrlQuery query) {
  return universe_.net.post(request(path, query), data);
}

QNetworkReply *Session::put(const QString &path, QJsonObject body) {
  return universe_.net.put(request(path), encode(body));
}

ContentFetch *Session::get(const Content &content) {
  auto url = content.url_on(homeserver_);
  QUrlQuery query;
  query.addQueryItem("access_token", access_token_);
  url.setQuery(query);
  QNetworkRequest req(url);
  auto reply = universe_.net.get(req);
  auto result = new ContentFetch(reply);
  connect(reply, &QNetworkReply::finished, [content, reply, result]() {
      reply->deleteLater();
      if(reply->error()) {
        result->error(reply->errorString());
      } else {
        result->finished(content,
                         reply->header(QNetworkRequest::ContentTypeHeader).toString(),
                         reply->header(QNetworkRequest::ContentDispositionHeader).toString(),
                         reply->readAll());
      }
    });
  return result;
}

ContentFetch *Session::get_thumbnail(const Content &content, const QSize &size, ThumbnailMethod method) {
  QUrl url(homeserver_);
  url.setPath("/_matrix/media/r0/thumbnail/" % content.host() % "/" % content.id());
  QUrlQuery query;
  query.addQueryItem("access_token", access_token_);
  query.addQueryItem("width", QString::number(size.width()));
  query.addQueryItem("height", QString::number(size.height()));
  query.addQueryItem("method", method == ThumbnailMethod::SCALE ? "scale" : "crop");
  url.setQuery(query);
  QNetworkRequest req(url);
  auto reply = universe_.net.get(req);
  auto result = new ContentFetch(reply);
  connect(reply, &QNetworkReply::finished, [content, reply, result]() {
      reply->deleteLater();
      if(reply->error()) {
        result->error(reply->errorString());
      } else {
        result->finished(content,
                         reply->header(QNetworkRequest::ContentTypeHeader).toString(),
                         reply->header(QNetworkRequest::ContentDispositionHeader).toString(),
                         reply->readAll());
      }
    });
  return result;
}

}
