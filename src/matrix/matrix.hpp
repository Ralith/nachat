#ifndef NATIVE_CHAT_MATRIX_MATRIX_H_
#define NATIVE_CHAT_MATRIX_MATRIX_H_

#include <QtNetwork>

#include <memory>
#include <experimental/optional>
#include <unordered_map>

#include "span.h"

#include "Room.hpp"

namespace matrix {

namespace proto {
struct Sync;
}

class Session : public QObject {
  Q_OBJECT

public:
  explicit Session(QNetworkAccessManager& net, QUrl homeserver, QString access_token);

  QString const& access_token() const { return access_token_; }

  void log_out();

  bool synced() const { return synced_; }
  std::vector<Room *> rooms();

signals:
  void logged_out();
  void error(QString message);
  void synced_changed();
  void rooms_changed();

private:
  QNetworkAccessManager& net_;
  const QUrl homeserver_;
  QString access_token_;
  std::unordered_map<std::string, Room> rooms_;
  bool synced_;
  QString next_batch_;

  QNetworkRequest request(QString path, QUrlQuery query = QUrlQuery());

  void handle_sync_reply(QNetworkReply *);
  void process_sync(proto::Sync sync);
};

class Matrix : public QObject {
  Q_OBJECT

public:
  explicit Matrix(QNetworkAccessManager &net, QObject *parent = 0);

  void login(QUrl homeserver, QString username, QString password);

signals:
  void logged_in(Session* session);
  void login_error(QString message);

private:
  QNetworkAccessManager &net_;
};

}

#endif
