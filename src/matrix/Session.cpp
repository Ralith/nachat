#include "Session.hpp"

#include <QTimer>

#include "utils.hpp"
#include "matrix.hpp"
#include "parse.hpp"

namespace matrix {

Session::Session(Matrix& universe, QUrl homeserver, QString user_id, QString access_token)
    : universe_(universe), homeserver_(homeserver), user_id_(user_id), access_token_(access_token), synced_(false) {
  QUrlQuery query;
  query.addQueryItem("filter", encode({
        {"room", QJsonObject{
            {"timeline", QJsonObject{
                {"limit", 0}
              }},
          }}
      }));
  query.addQueryItem("full_state", "true");
  sync(query);
}

void Session::sync(QUrlQuery query) {
  auto reply = universe_.net.get(request("client/r0/sync", query));
  connect(reply, &QNetworkReply::finished, [this, reply](){
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
    qDebug() << "loading state...";
    room.load_state(joined_room.state.events);
    qDebug() << room.state().pretty_name() << "loaded, dispatching events...";
    if(new_room) joined(room);
    room.dispatch(joined_room);
    qDebug() << "done";
  }
  sync_complete();
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

}
