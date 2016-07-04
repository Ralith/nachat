#ifndef NATIVE_CHAT_MATRIX_MATRIX_H_
#define NATIVE_CHAT_MATRIX_MATRIX_H_

#include <memory>
#include <experimental/optional>
#include <unordered_map>
#include <chrono>

#include <QtNetwork>

#include "span.h"

#include "Room.hpp"

namespace matrix {

namespace proto {
struct Sync;
}

class Matrix;

class Session : public QObject {
  Q_OBJECT

public:
  Session(Matrix& universe, QUrl homeserver, QString user_id, QString access_token);

  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;

  const QString &access_token() const { return access_token_; }
  const QString &user_id() const { return user_id_; }

  void log_out();

  bool synced() const { return synced_; }
  std::vector<Room *> rooms();

signals:
  void logged_out();
  void error(QString message);
  void synced_changed();
  void rooms_changed();
  void sync_progress(qint64 received, qint64 total);

private:
  Matrix &universe_;
  const QUrl homeserver_;
  const QString user_id_;
  QString access_token_;
  std::unordered_map<std::string, Room> rooms_;
  bool synced_;
  QString next_batch_;

  std::chrono::steady_clock::time_point last_sync_error_;
  // Last time a sync failed. Used to ensure we don't spin if errors happen quickly.

  QNetworkRequest request(QString path, QUrlQuery query = QUrlQuery());

  void sync(QUrlQuery query);
  void handle_sync_reply(QNetworkReply *);
  void dispatch(proto::Sync sync);
};

class Matrix : public QObject {
  Q_OBJECT

public:
  QNetworkAccessManager &net;

  explicit Matrix(QNetworkAccessManager &net, QObject *parent = 0);

  Matrix(const Matrix &) = delete;
  Matrix &operator=(const Matrix &) = delete;

  void login(QUrl homeserver, QString username, QString password);

signals:
  void logged_in(Session* session);
  void login_error(QString message);
};

}

#endif
