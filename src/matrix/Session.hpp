#ifndef NATIVE_CHAT_MATRIX_SESSION_H_
#define NATIVE_CHAT_MATRIX_SESSION_H_

#include <unordered_map>
#include <chrono>

#include <QtNetwork>
#include <QObject>
#include <QString>
#include <QUrlQuery>

#include <lmdb++.h>

#include "../QStringHash.hpp"

#include "Room.hpp"
#include "Content.hpp"

namespace matrix {

namespace proto {
struct Sync;
}

class Matrix;

class JoinRequest : public QObject {
  Q_OBJECT

public:
  explicit JoinRequest(QObject *parent) : QObject(parent) {}

signals:
  void success(const RoomID &id);
  void error(const QString &msg);
};

class ContentFetch : public QObject {
  Q_OBJECT

public:
  ContentFetch(QObject *parent = nullptr) : QObject(parent) {}

signals:
  void finished(const Content &content, const QString &type, const QString &disposition, const QByteArray &data);
  void error(const QString &msg);
};

enum class ThumbnailMethod { CROP, SCALE };

class Session : public QObject {
  Q_OBJECT

public:
  Session(Matrix& universe, QUrl homeserver, QString user_id, QString access_token,
          lmdb::env &&env, lmdb::dbi &&state_db, lmdb::dbi &&room_db);

  // Returns ownership
  static Session *create(Matrix& universe, QUrl homeserver, QString user_id, QString access_token);

  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;

  const QString &access_token() const { return access_token_; }
  const UserID &user_id() const { return user_id_; }
  const QUrl &homeserver() const { return homeserver_; }

  void log_out();

  bool synced() const { return synced_; }
  std::vector<Room *> rooms();
  Room *room_from_id(const RoomID &r) {
    auto it = rooms_.find(r);
    if(it == rooms_.end()) return nullptr;
    return &it->second;
  }
  const Room *room_from_id(const RoomID &r) const {
    auto it = rooms_.find(r);
    if(it == rooms_.end()) return nullptr;
    return &it->second;
  }

  size_t buffer_size() const { return buffer_size_; }
  void set_buffer_size(size_t size) { buffer_size_ = size; }

  QNetworkReply *get(const QString &path, QUrlQuery query = QUrlQuery());

  QNetworkReply *post(const QString &path, QJsonObject body = QJsonObject(), QUrlQuery query = QUrlQuery());
  QNetworkReply *post(const QString &path, QIODevice *data, const QString &content_type, const QString &filename);

  QNetworkReply *put(const QString &path, QJsonObject body);

  ContentFetch *get(const Content &);

  ContentFetch *get_thumbnail(const Content &, const QSize &size, ThumbnailMethod method = ThumbnailMethod::SCALE);

  QString get_transaction_id();

  JoinRequest *join(const QString &id_or_alias);

signals:
  void logged_out();
  void error(QString message);
  void synced_changed();
  void joined(matrix::Room &room);
  void sync_progress(qint64 received, qint64 total);
  void sync_complete();

private:
  Matrix &universe_;
  const QUrl homeserver_;
  const QString user_id_;
  QString access_token_;
  lmdb::env env_;
  lmdb::dbi state_db_, room_db_;
  size_t buffer_size_;
  std::unordered_map<RoomID, Room, QStringHash> rooms_;
  bool synced_;
  QString next_batch_;

  std::chrono::steady_clock::time_point last_sync_error_;
  // Last time a sync failed. Used to ensure we don't spin if errors happen quickly.

  QNetworkRequest request(const QString &path, QUrlQuery query = QUrlQuery(), const QString &content_type = "application/json");

  void sync(QUrlQuery query = QUrlQuery());
  void handle_sync_reply(QNetworkReply *);
  void dispatch(lmdb::txn &txn, proto::Sync sync);
};

}

#endif
