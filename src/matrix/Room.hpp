#ifndef NATIVE_CHAT_MATRIX_ROOM_H_
#define NATIVE_CHAT_MATRIX_ROOM_H_

#include <vector>
#include <experimental/optional>
#include <unordered_map>

#include <QString>
#include <QObject>
#include <QUrl>

#include <span.h>

namespace matrix {

namespace proto {
struct JoinedRoom;
struct Event;
}

struct Message {
  QString body;
};

class User : public QObject {
  Q_OBJECT

public:
  User(QString id) : id_(id) {}

  const QString &id() const { return id_; }
  const std::experimental::optional<QString> &display_name() const { return display_name_; }
  const std::experimental::optional<QUrl> &avatar_url() const { return avatar_url_; }
  bool invite_pending() const { return invite_pending_; }

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
  std::experimental::optional<QString> display_name_;
  std::experimental::optional<QUrl> avatar_url_;
  bool invite_pending_ = false;
};

class Room : public QObject {
  Q_OBJECT

public:
  Room(QString id) : id_(id) {}

  const QString &id() const { return id_; }

  std::vector<const User *> users() const;
  std::vector<const Message *> messages() const;

  gsl::span<const QString> aliases() const { return aliases_; }
  const std::experimental::optional<QString> &name() const { return name_; }
  QString display_name() const;
  uint64_t highlight_count() const { return highlight_count_; }
  uint64_t notification_count() const { return notification_count_; }

  void dispatch(const proto::JoinedRoom &);

signals:
  void messages_changed();
  void users_changed(gsl::span<const User *const> new_users);
  void name_changed();
  void aliases_changed();
  void highlight_count_changed();
  void notification_count_changed();

private:
  QString id_;
  std::vector<QString> aliases_;
  std::experimental::optional<QString> name_;
  std::unordered_map<std::string, User> users_;
  uint64_t highlight_count_ = 0, notification_count_ = 0;
};

}

#endif
