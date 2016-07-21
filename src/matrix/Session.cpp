#include "Session.hpp"

#include <stdexcept>

#include <QTimer>
#include <QUrl>

#include "utils.hpp"
#include "Matrix.hpp"
#include "parse.hpp"

namespace matrix {

Session *Session::create(Matrix& universe, QUrl homeserver, QString user_id, QString access_token) {
  auto env = lmdb::env::create();
  env.set_mapsize(128UL * 1024UL * 1024UL);  // 128MB should be enough for anyone!
  env.set_max_dbs(1024UL);                   // maximum rooms plus two

  QString state_path = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) % "/" % QString::fromUtf8(user_id.toUtf8().toHex() % "/state");
  if(!QDir().mkpath(state_path)) {
    throw std::runtime_error(("unable to create state directory at " + state_path).toStdString().c_str());
  }

  env.open(state_path.toStdString().c_str());

  auto txn = lmdb::txn::begin(env);
  auto state_db = lmdb::dbi::open(txn, "state", MDB_CREATE);
  auto room_db = lmdb::dbi::open(txn, "rooms", MDB_CREATE);
  txn.commit();

  return new Session(universe, std::move(homeserver), std::move(user_id), std::move(access_token),
                     std::move(env), std::move(state_db), std::move(room_db));
}

static constexpr char POLL_TIMEOUT_MS[] = "50000";
static const lmdb::val next_batch_key("next_batch");

static std::string room_dbname(const QString &room_id) { return ("r." + room_id).toStdString(); }

Session::Session(Matrix& universe, QUrl homeserver, QString user_id, QString access_token,
                 lmdb::env &&env, lmdb::dbi &&state_db, lmdb::dbi &&room_db)
    : universe_(universe), homeserver_(homeserver), user_id_(user_id), access_token_(access_token),
      env_(std::move(env)), state_db_(std::move(state_db)), room_db_(std::move(room_db)),
      buffer_size_(50), synced_(false) {
  {
    auto txn = lmdb::txn::begin(env_, nullptr, MDB_RDONLY);
    lmdb::val stored_batch;
    if(lmdb::dbi_get(txn, state_db_, next_batch_key, stored_batch)) {
      next_batch_ = QString::fromUtf8(stored_batch.data(), stored_batch.size());
      qDebug() << "resuming from" << next_batch_;

      lmdb::val room;
      lmdb::val state;
      auto cursor = lmdb::cursor::open(txn, room_db_);
      while(cursor.get(room, state, MDB_NEXT)) {
        auto id = QString::fromUtf8(room.data(), room.size());
        rooms_.emplace(std::piecewise_construct,
                       std::forward_as_tuple(id),
                       std::forward_as_tuple(universe_, *this, id,
                                             QJsonDocument::fromBinaryData(QByteArray(state.data(), state.size())).object(),
                                             env_, txn, lmdb::dbi::open(txn, room_dbname(id).c_str())));
      }
    } else {
      qDebug() << "starting from scratch";
    }
    txn.commit();
  }

  QUrlQuery query;
  query.addQueryItem("filter", encode({
        {"room", QJsonObject{
            {"timeline", QJsonObject{
                {"limit", static_cast<int>(buffer_size_)}
              }},
          }}
      }));
  sync(query);
}

void Session::sync(QUrlQuery query) {
  if(next_batch_.isNull()) {
    query.addQueryItem("full_state", "true");
  } else {
    query.addQueryItem("since", next_batch_);
    query.addQueryItem("timeout", POLL_TIMEOUT_MS);
  }
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
    QString current_batch = next_batch_;
    try {
      auto s = parse_sync(r.object);
      auto txn = lmdb::txn::begin(env_);
      auto batch_utf8 = s.next_batch.toUtf8();
      lmdb::dbi_put(txn, state_db_, next_batch_key, lmdb::val(batch_utf8.data(), batch_utf8.size()));
      next_batch_ = s.next_batch;
      dispatch(txn, std::move(s));
      txn.commit();
      synced_ = true;
    } catch(lmdb::runtime_error &e) {
      synced_ = false;
      next_batch_ = current_batch;
      error(e.what());
    }
  }
  if(was_synced != synced_) synced_changed();

  auto now = std::chrono::steady_clock::now();
  constexpr std::chrono::steady_clock::duration RETRY_INTERVAL = 10s;
  auto since_last_error = now - last_sync_error_;
  if(!synced_ && (since_last_error < RETRY_INTERVAL)) {
    QTimer::singleShot(std::chrono::duration_cast<std::chrono::milliseconds>(RETRY_INTERVAL - since_last_error).count(),
                       [this](){
                         sync();
                       });
  } else {
    sync();
  }

  if(!synced_) {
    last_sync_error_ = now;
  }
}

void Session::dispatch(lmdb::txn &txn, proto::Sync sync) {
  // TODO: Exception-safety: copy all room states, update and them and db, commit, swap copy/orig, emit signals
  for(auto &joined_room : sync.rooms.join) {
    auto it = rooms_.find(joined_room.id);
    bool new_room = false;
    if(it == rooms_.end()) {
      auto db = lmdb::dbi::open(txn, room_dbname(joined_room.id).c_str(), MDB_CREATE);

      it = rooms_.emplace(std::piecewise_construct,
                          std::forward_as_tuple(joined_room.id),
                          std::forward_as_tuple(universe_, *this, joined_room.id, QJsonObject(),
                                                env_, txn, std::move(db))).first;
      new_room = true;
    }
    auto &room = it->second;
    room.load_state(txn, joined_room.state.events);
    room.dispatch(txn, joined_room);
    {  // Mandatory write, since either the buffer or state has almost certainly changed
      auto data = QJsonDocument(room.to_json()).toBinaryData();
      auto utf8 = room.id().toUtf8();
      lmdb::dbi_put(txn, room_db_, lmdb::val(utf8.data(), utf8.size()), lmdb::val(data.data(), data.size()));
    }
    if(new_room) joined(room);
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
