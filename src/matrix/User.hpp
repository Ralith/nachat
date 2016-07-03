#ifndef NATIVE_CHAT_MATRIX_USER_H_
#define NATIVE_CHAT_MATRIX_USER_H_

#include <QObject>
#include <QString>
#include <QUrl>

namespace matrix {

namespace proto {
struct Event;
}

class User : public QObject {
  Q_OBJECT

public:
  User(QString id) : id_(id) {}

  User(const User &) = delete;
  User &operator=(const User &) = delete;

  const QString &id() const { return id_; }
  const QString &display_name() const { return display_name_; }
  const QUrl &avatar_url() const { return avatar_url_; }
  bool invite_pending() const { return invite_pending_; }

  const QString &pretty_name() const { return display_name_.isEmpty() ? id_ : display_name_; }

  void dispatch(const proto::Event &);

signals:
  void joined();
  void left();
  void banned();
  void display_name_changed();
  void avatar_url_changed();
  void invite_pending_changed();

private:
  const QString id_;
  QString display_name_;  // Optional
  QUrl avatar_url_;       // Optional
  bool invite_pending_ = false;
};

}

#endif
