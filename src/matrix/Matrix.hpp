#ifndef NATIVE_CHAT_MATRIX_MATRIX_H_
#define NATIVE_CHAT_MATRIX_MATRIX_H_

#include "Room.hpp"

class QNetworkAccessManager;

namespace matrix {

class Matrix : public QObject {
  Q_OBJECT

public:
  explicit Matrix(QNetworkAccessManager &net, QObject *parent = 0);

  Matrix(const Matrix &) = delete;
  Matrix &operator=(const Matrix &) = delete;

  void login(QUrl homeserver, QString username, QString password);

signals:
  void logged_in(const QString &user_id, const QString &access_token);
  void login_error(QString message);

private:
  friend class Session;
  QNetworkAccessManager &net;
};

}

#endif
