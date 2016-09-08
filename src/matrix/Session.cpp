#include "Session.hpp"

#include <stdexcept>

#include <QtNetwork>
#include <QTimer>
#include <QUrl>

#include "utils.hpp"
#include "Matrix.hpp"
#include "proto.hpp"

namespace matrix {

constexpr uint64_t CACHE_FORMAT_VERSION = 4;
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

struct SessionInit {
  lmdb::env env;
  lmdb::dbi state;
  lmdb::dbi room;
};

static SessionInit session_init(const UserID &user_id) {
  auto env = lmdb::env::create();
  env.set_mapsize(128UL * 1024UL * 1024UL);  // 128MB should be enough for anyone!
  env.set_max_dbs(1024UL);                   // maximum rooms plus two

  QString state_path = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) % "/" % QString::fromUtf8(user_id.value().toUtf8().toHex() % "/state");
  bool fresh = !QFile::exists(state_path);
  if(!QDir().mkpath(state_path)) {
    throw std::runtime_error(("unable to create state directory at " + state_path).toStdString().c_str());
  }

  try {
    env.open(state_path.toStdString().c_str());
  } catch(const lmdb::error &e) {
    if(e.code() != MDB_VERSION_MISMATCH && e.code() != MDB_INVALID) throw;
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

  return SessionInit{std::move(env), std::move(state_db), std::move(room_db)};
}

Session::Session(Matrix& universe, QUrl homeserver, UserID user_id, QString access_token)
  : Session{universe, homeserver, user_id, access_token, session_init(user_id)} {}

static std::string room_dbname(const RoomID &room_id) { return ("r." + room_id.value()).toStdString(); }

Session::Session(Matrix& universe, QUrl homeserver, UserID user_id, QString access_token,
                 SessionInit &&init)
    : universe_(universe), homeserver_(homeserver), user_id_(user_id), access_token_(access_token),
      env_(std::move(init.env)), state_db_(std::move(init.state)), room_db_(std::move(init.room)),
      buffer_size_(50), synced_(false) {
  {
    auto txn = lmdb::txn::begin(env_, nullptr, MDB_RDONLY);
    lmdb::val stored_batch;
    if(lmdb::dbi_get(txn, state_db_, next_batch_key, stored_batch)) {
      next_batch_ = SyncCursor{QString::fromUtf8(stored_batch.data(), stored_batch.size())};
      qDebug() << "resuming from" << next_batch_->value();

      lmdb::val room;
      lmdb::val state;
      auto cursor = lmdb::cursor::open(txn, room_db_);
      while(cursor.get(room, state, MDB_NEXT)) {
        auto id = RoomID(QString::fromUtf8(room.data(), room.size()));
        auto &&member_db = lmdb::dbi::open(txn, room_dbname(id).c_str(), MDB_CREATE);
        std::vector<Member> members;
        {
          auto member_cursor = lmdb::cursor::open(txn, member_db);
          lmdb::val member_id;
          lmdb::val member_content;
          while(member_cursor.get(member_id, member_content, MDB_NEXT)) {
            members.emplace_back(UserID{QString::fromUtf8(member_id.data(), member_id.size())},
                                 event::room::MemberContent{event::Content{
                                     QJsonDocument::fromBinaryData(QByteArray{member_content.data(), static_cast<int>(member_content.size())}).object()}});
          }
        }
        auto &room = rooms_.emplace(std::piecewise_construct,
                                    std::forward_as_tuple(id),
                                    std::forward_as_tuple(universe_, *this, id,
                                                          QJsonDocument::fromBinaryData(QByteArray(state.data(), state.size())).object(),
                                                          members)).first->second;
        room.members = std::move(member_db);
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
  if(!next_batch_) {
    query.addQueryItem("full_state", "true");
  } else {
    query.addQueryItem("since", next_batch_->value());
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
    dispatch(parse_sync(r.object));
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

void Session::dispatch(const proto::Sync &sync) {
  for(auto &joined_room : sync.rooms.join) {
    auto it = rooms_.find(joined_room.id);
    if(it == rooms_.end()) {
      auto &room = rooms_.emplace(std::piecewise_construct,
                                  std::forward_as_tuple(joined_room.id),
                                  std::forward_as_tuple(universe_, *this, joined_room)).first->second;
      const auto id = joined_room.id;
      connect(&room.room, &Room::member_changed, [&room](const UserID &id,
                                                         const event::room::MemberContent &old,
                                                         const event::room::MemberContent &current) {
          (void)old;
          room.member_changes.emplace_back(id, current);
        });
      for(const auto m : room.room.state().members()) {
        room.member_changes.emplace_back(m->first, m->second);
      }

      joined(room.room);
    } else {
      auto &room = it->second;
      room.room.dispatch(joined_room);
    }
  }

  next_batch_ = sync.next_batch;

  update_cache(sync);

  synced_ = true;

  sync_complete();
}

void Session::update_cache(const proto::Sync &sync) {
  try {
    std::vector<std::pair<const RoomID, lmdb::dbi>> new_member_dbs;

    auto txn = lmdb::txn::begin(env_);

    auto batch_utf8 = sync.next_batch.value().toUtf8();
    lmdb::dbi_put(txn, state_db_, next_batch_key, lmdb::val(batch_utf8.data(), batch_utf8.size()));

    for(auto &joined_room : sync.rooms.join) {
      auto &room = rooms_.at(joined_room.id);

      lmdb::dbi *member_db;
      {
        if(!room.members) {
          // We defer adding to member_dbs_ until after commit because the handle will be invalidated if the transaction fails
          new_member_dbs.emplace_back(joined_room.id, lmdb::dbi::open(txn, room_dbname(joined_room.id).c_str(), MDB_CREATE));
          member_db = &new_member_dbs.back().second;
        } else {
          member_db = &*room.members;
        }
      }

      {
        auto data = QJsonDocument(room.room.to_json()).toBinaryData();
        auto utf8 = joined_room.id.value().toUtf8();
        lmdb::dbi_put(txn, room_db_, lmdb::val(utf8.data(), utf8.size()), lmdb::val(data.data(), data.size()));
      }

      for(const auto &m : room.member_changes) {
        auto id_utf8 = m.first.value().toUtf8();
        switch(m.second.membership()) {
        case Membership::INVITE:
        case Membership::JOIN: {
          auto data = QJsonDocument(m.second.json()).toBinaryData();
          lmdb::dbi_put(txn, *member_db, lmdb::val(id_utf8.data(), id_utf8.size()), lmdb::val(data.data(), data.size()));
          break;
        }
        case Membership::LEAVE:
        case Membership::BAN:
          lmdb::dbi_del(txn, *member_db, lmdb::val(id_utf8.data(), id_utf8.size()), nullptr);
          break;
        }
      }
    }

    txn.commit();

    // Post-success
    for(auto &db : new_member_dbs) {
      rooms_.at(db.first).members = std::move(db.second);
    }

    for(auto &joined_room : sync.rooms.join) {
      auto &room = rooms_.at(joined_room.id);
      room.member_changes.clear();
    }

  } catch(lmdb::runtime_error &e) {
    // TODO: Handle out of space/databases and retry
    error(e.what());
  }
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
                 [](auto &x) { return &x.second.room; });
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

QNetworkReply *Session::put(const QString &path, QJsonObject body) {
  auto reply = universe_.net.put(request(path), encode(body));
  connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
  return reply;
}

ContentFetch *Session::get(const Content &content) {
  auto reply = get("media/r0/download/" % content.host() % "/" % content.id());
  auto result = new ContentFetch(reply);
  connect(reply, &QNetworkReply::finished, [reply, result]() {
      if(reply->error()) {
        result->error(reply->errorString());
      } else {
        result->finished(reply->header(QNetworkRequest::ContentTypeHeader).toString(),
                         reply->header(QNetworkRequest::ContentDispositionHeader).toString(),
                         reply->readAll());
      }
    });
  return result;
}

ContentFetch *Session::get_thumbnail(const Thumbnail &t) {
  QUrlQuery query;
  query.addQueryItem("access_token", access_token_);
  query.addQueryItem("width", QString::number(t.size().width()));
  query.addQueryItem("height", QString::number(t.size().height()));
  query.addQueryItem("method", t.method() == ThumbnailMethod::SCALE ? "scale" : "crop");
  auto reply = get("media/r0/thumbnail/" % t.content().host() % "/" % t.content().id(), query);
  auto result = new ContentFetch(reply);
  connect(reply, &QNetworkReply::finished, [reply, result]() {
      if(reply->error()) {
        result->error(reply->errorString());
      } else {
        result->finished(reply->header(QNetworkRequest::ContentTypeHeader).toString(),
                         reply->header(QNetworkRequest::ContentDispositionHeader).toString(),
                         reply->readAll());
      }
    });
  return result;
}

TransactionID Session::get_transaction_id() {
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

  return TransactionID{QString::number(value, 36)};
}

JoinRequest *Session::join(const QString &id_or_alias) {
  auto reply = post("client/r0/join/" + QUrl::toPercentEncoding(id_or_alias), {});
  auto req = new JoinRequest(reply);
  connect(reply, &QNetworkReply::finished, [reply, req]() {
      auto r = decode(reply);
      if(r.error) {
        req->error(*r.error);
      } else {
        req->success(RoomID(r.object["room_id"].toString()));
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

ContentPost *Session::upload(QIODevice &data, const QString &content_type, const QString &filename) {
  QUrlQuery query;
  query.addQueryItem("filename", filename);
  auto reply = universe_.net.post(request("media/r0/upload", query, content_type), &data);
  auto result = new ContentPost(reply);
  connect(reply, &QNetworkReply::finished, [result, reply]() {
      reply->deleteLater();
      auto r = decode(reply);
      if(r.error) {
        result->error(*r.error);
      } else {
        result->success(r.object["content_uri"].toString());
      }
    });
  connect(reply, &QNetworkReply::uploadProgress, result, &ContentPost::progress);
  return result;
}

}
