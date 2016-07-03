#ifndef NATIVE_CHAT_MATRIX_ROOM_H_
#define NATIVE_CHAT_MATRIX_ROOM_H_

#include <vector>
#include <list>
#include <unordered_set>

#include <QString>
#include <QObject>

#include <span.h>

namespace matrix {

class User;
class Matrix;

namespace proto {
struct JoinedRoom;
}

struct Message {
  QString body;
};

class Room : public QObject {
  Q_OBJECT

public:
  Room(Matrix &universe, QString id) : universe_(universe), id_(id) {}

  Room(const Room &) = delete;
  Room &operator=(const Room &) = delete;

  const QString &id() const { return id_; }

  std::vector<const User *> users() const;
  std::vector<const Message *> messages() const;

  gsl::span<const QString> aliases() const { return aliases_; }
  const QString &name() const { return name_; }
  uint64_t highlight_count() const { return highlight_count_; }
  uint64_t notification_count() const { return notification_count_; }
  const std::list<Message> &messages() { return messages_; }

  QString pretty_name() const;

  void dispatch(const proto::JoinedRoom &);

signals:
  void messages_changed();
  void users_changed(gsl::span<const User *const> new_users);
  void name_changed();
  void aliases_changed();
  void highlight_count_changed();
  void notification_count_changed();

private:
  Matrix &universe_;
  QString id_;
  std::vector<QString> aliases_;
  QString name_;
  std::unordered_set<User *> users_;
  std::list<Message> messages_;
  uint64_t highlight_count_ = 0, notification_count_ = 0;
};

}

#endif
