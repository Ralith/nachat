#ifndef NATIVE_CHAT_MATRIX_MATRIX_H_
#define NATIVE_CHAT_MATRIX_MATRIX_H_

#include <memory>
#include <experimental/optional>
#include <unordered_map>
#include <chrono>

#include "Room.hpp"

class QNetworkAccessManager;

namespace matrix {

class Session;

class Matrix : public QObject {
  Q_OBJECT

public:
  explicit Matrix(QNetworkAccessManager &net, QObject *parent = 0);

  Matrix(const Matrix &) = delete;
  Matrix &operator=(const Matrix &) = delete;

  void login(QUrl homeserver, QString username, QString password);

signals:
  void logged_in(Session* session);
  void login_error(QString message);

private:
  friend class Session;
  QNetworkAccessManager &net;
};

}

#endif
