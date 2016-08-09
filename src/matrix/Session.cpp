#include "Session.hpp"

#include <stdexcept>

#include <QtNetwork>
#include <QTimer>
#include <QUrl>
#include <QDataStream>

#include "utils.hpp"
#include "Matrix.hpp"
#include "parse.hpp"

namespace matrix {

constexpr uint64_t CACHE_FORMAT_VERSION = 1;
// Bumped every time a backwards-incompatible format change is made, a
// corruption bug is fixed, or a previously ignored class of state is
// persisted

static constexpr char POLL_TIMEOUT_MS[] = "50000";

static const lmdb::val next_batch_key("next_batch");
static const lmdb::val transaction_id_key("transaction_id");
static const lmdb::val cache_format_version_key("cache_format_version");

template<typename T, typename = std::enable_if_t<std::is_integral<T>::value>>
constexpr T from_little_endian(const uint8_t *x) {
  T result{0};
  for(size_t i = 0; i < sizeof(T); ++i) {
    result |= static_cast<T>(x[i]) << (8*i);
  }
  return result;
}

template<typename T, typename = std::enable_if_t<std::is_integral<T>::value>>
constexpr void to_little_endian(T v, uint8_t *x) {
  for(size_t i = 0; i < sizeof(T); ++i) {
    x[i] = (v >> (8*i)) & 0xFF;
  }
}

std::unique_ptr<Session> Session::create(Matrix& universe, QUrl homeserver, QString user_id, QString access_token) {
  auto env = lmdb::env::create();
  env.set_mapsize(128UL * 1024UL * 1024UL);  // 128MB should be enough for anyone!
  env.set_max_dbs(1024UL);                   // maximum rooms plus two

  QString state_path = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) % "/" % QString::fromUtf8(user_id.toUtf8().toHex() % "/state");
  bool fresh = !QFile::exists(state_path);
  if(!QDir().mkpath(state_path)) {
    throw std::runtime_error(("unable to create state directory at " + state_path).toStdString().c_str());
  }

  try {
    env.open(state_path.toStdString().c_str());
  } catch(const lmdb::version_mismatch_error &e) {
    qDebug() << "resetting cache due to LMDB version mismatch:" << e.what();
    QDir state_dir(state_path);
    for(const auto &file : state_dir.entryList(QDir::NoDotAndDotDot)) {
      if(!state_dir.remove(file)) {
        throw std::runtime_error(("unable to delete state file " + state_path + file).toStdString().c_str());
      }
    }
    env.open(state_path.toStdString().c_str());
  }

  auto txn = lmdb::txn::begin(env);
  auto state_db = lmdb::dbi::open(txn, "state", MDB_CREATE);
  auto room_db = lmdb::dbi::open(txn, "rooms", MDB_CREATE);

  if(!fresh) {
    bool compatible = false;
    lmdb::val x;
    if(lmdb::dbi_get(txn, state_db, cache_format_version_key, x)) {
      compatible = CACHE_FORMAT_VERSION == from_little_endian<uint64_t>(x.data<const uint8_t>());
    }
    if(!compatible) {
      qDebug() << "resetting cache due to breaking changes or fixes";
      lmdb::dbi_drop(txn, state_db, false);
      lmdb::dbi_drop(txn, room_db, false);
      fresh = true;
    }
  }

  if(fresh) {
    uint8_t data[8];
    to_little_endian(CACHE_FORMAT_VERSION, data);
    lmdb::val val(data, sizeof(data));
    lmdb::dbi_put(txn, state_db, cache_format_version_key, val);
  }

  txn.commit();

  return std::make_unique<Session>(universe, std::move(homeserver), std::move(user_id), std::move(access_token),
                                   std::move(env), std::move(state_db), std::move(room_db));
}

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

  sync_retry_timer_.setSingleShot(true);
  connect(&sync_retry_timer_, &QTimer::timeout, this, static_cast<void (Session::*)()>(&Session::sync));

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

void Session::sync() {
  // This method exists so that we can hook Qt signals to it without worrying about default arguments.
  sync(QUrlQuery());
}

void Session::sync(QUrlQuery query) {
  if(next_batch_.isNull()) {
    query.addQueryItem("full_state", "true");
  } else {
    query.addQueryItem("since", next_batch_);
    query.addQueryItem("timeout", POLL_TIMEOUT_MS);
  }
  sync_reply_ = get("client/r0/sync", query);
  connect(sync_reply_, &QNetworkReply::finished, this, &Session::handle_sync_reply);
  connect(sync_reply_, &QNetworkReply::downloadProgress, this, &Session::sync_progress);
}

void Session::handle_sync_reply() {
  sync_progress(0, 0);
  if(sync_reply_->size() > (1 << 12)) {
    qDebug() << "sync is" << sync_reply_->size() << "bytes";
  }

  using namespace std::chrono_literals;

  auto r = decode(sync_reply_);
  bool was_synced = synced_;
  if(r.error) {
    synced_ = false;
    error(*r.error);
  } else {
    QString current_batch = next_batch_;
    try {
      auto s = parse_sync(r.object);
      auto txn = lmdb::txn::begin(env_);
      active_txn_ = &txn;
      try {
        auto batch_utf8 = s.next_batch.toUtf8();
        lmdb::dbi_put(txn, state_db_, next_batch_key, lmdb::val(batch_utf8.data(), batch_utf8.size()));
        next_batch_ = s.next_batch;
        dispatch(txn, std::move(s));
        txn.commit();
      } catch(...) {
        active_txn_ = nullptr;
        throw;
      }
      active_txn_ = nullptr;
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
    sync_retry_timer_.start(std::chrono::duration_cast<std::chrono::milliseconds>(RETRY_INTERVAL - since_last_error).count());
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
    // Mandatory write, since either the buffer or state has almost certainly changed
    cache_state(txn, room);
    if(new_room) joined(room);
  }
  sync_complete();
}

void Session::cache_state(lmdb::txn &txn, const Room &room) {
  auto data = QJsonDocument(room.to_json()).toBinaryData();
  auto utf8 = room.id().toUtf8();
  lmdb::dbi_put(txn, room_db_, lmdb::val(utf8.data(), utf8.size()), lmdb::val(data.data(), data.size()));
}

void Session::log_out() {
  auto reply = post("client/r0/logout", {});
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

QNetworkRequest Session::request(const QString &path, QUrlQuery query, const QString &content_type) {
  QUrl url(homeserver_);
  url.setPath("/_matrix/" + path, QUrl::StrictMode);
  query.addQueryItem("access_token", access_token_);
  url.setQuery(query);
  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::ContentTypeHeader, content_type);
  return req;
}

QNetworkReply *Session::get(const QString &path, QUrlQuery query) {
  auto reply = universe_.net.get(request(path, query));
  connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
  return reply;
}

QNetworkReply *Session::post(const QString &path, QJsonObject body, QUrlQuery query) {
  auto reply = universe_.net.post(request(path, query), encode(body));
  connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
  return reply;
}

QNetworkReply *Session::post(const QString &path, QIODevice *data, const QString &content_type, const QString &filename) {
  QUrlQuery query;
  query.addQueryItem("filename", filename);
  auto reply = universe_.net.post(request(path, query, content_type), data);
  connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
  return reply;
}

QNetworkReply *Session::put(const QString &path, QJsonObject body) {
  auto reply = universe_.net.put(request(path), encode(body));
  connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
  return reply;
}

ContentFetch *Session::get(const Content &content) {
  auto reply = get("media/r0/download/" % content.host() % "/" % content.id());
  auto result = new ContentFetch(reply);
  connect(reply, &QNetworkReply::finished, [content, reply, result]() {
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
  QUrlQuery query;
  query.addQueryItem("access_token", access_token_);
  query.addQueryItem("width", QString::number(size.width()));
  query.addQueryItem("height", QString::number(size.height()));
  query.addQueryItem("method", method == ThumbnailMethod::SCALE ? "scale" : "crop");
  auto reply = get("media/r0/thumbnail/" % content.host() % "/" % content.id(), query);
  auto result = new ContentFetch(reply);
  connect(reply, &QNetworkReply::finished, [content, reply, result]() {
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

QString Session::get_transaction_id() {
  auto txn = lmdb::txn::begin(env_);

  uint64_t value;
  lmdb::val x;
  if(lmdb::dbi_get(txn, state_db_, transaction_id_key, x)) {
    value = from_little_endian<uint64_t>(x.data<const uint8_t>());
  } else {
    value = 0;
  }

  uint8_t data[8];
  to_little_endian(value + 1, data);
  lmdb::val y(data, sizeof(data));
  lmdb::dbi_put(txn, state_db_, transaction_id_key, y);

  txn.commit();

  return QString::number(value, 36);
}

JoinRequest *Session::join(const QString &id_or_alias) {
  auto reply = post("client/r0/join/" + QUrl::toPercentEncoding(id_or_alias), {});
  auto req = new JoinRequest(reply);
  connect(reply, &QNetworkReply::finished, [reply, req]() {
      auto r = decode(reply);
      if(r.error) {
        req->error(*r.error);
      } else {
        req->success(r.object["room_id"].toString());
      }
    });
  return req;
}

QUrl Session::ensure_http(const QUrl &url) const {
  if(url.scheme() == "mxc") {
    return matrix::Content(url).url_on(homeserver());
  }
  return url;
}

void Session::cache_state(const Room &room) {
  if(active_txn_) return;       // State will be cached after sync processing completes
  auto txn = lmdb::txn::begin(env_);
  cache_state(txn, room);
  txn.commit();
}

}
