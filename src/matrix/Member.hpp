#ifndef NATIVE_CHAT_MATRIX_MEMBER_H_
#define NATIVE_CHAT_MATRIX_MEMBER_H_

#include <QObject>
#include <QString>
#include <QUrl>

namespace matrix {

class Room;

namespace proto {
struct Event;
}

class Member : public QObject {
  Q_OBJECT

public:
  Member(QString id) : id_(id) {}

  Member(const Member &) = delete;
  Member &operator=(const Member &) = delete;

  const QString &id() const { return id_; }
  const QString &display_name() const { return display_name_; }
  const QUrl &avatar_url() const { return avatar_url_; }
  bool invite_pending() const { return invite_pending_; }

  void dispatch(const proto::Event &);

signals:
  void joined();
  void left();
  void banned();
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
