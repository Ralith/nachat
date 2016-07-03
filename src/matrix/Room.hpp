#ifndef NATIVE_CHAT_MATRIX_ROOM_H_
#define NATIVE_CHAT_MATRIX_ROOM_H_

#include <vector>
#include <experimental/optional>

#include <QString>
#include <QObject>

#include <span.h>

namespace matrix {

struct Message {
  QString body;
};

struct User {
  QString id;
  std::experimental::optional<QString> display_name;
};

class Room : public QObject {
  Q_OBJECT

public:
  Room(QString id);

  const QString &id() const { return id_; }

  std::vector<const User *> users() const;
  std::vector<const Message *> messages() const;

  gsl::span<const QString> aliases() const { return aliases_; }
  const std::experimental::optional<QString> &name() const { return name_; }
  const QString &display_name() const { return name_ ? *name_ : (!aliases_.empty() ? aliases_[0] : id_); }

signals:
  void messages_changed();
  void users_changed();
  void name_changed();
  void aliases_changed();

private:
  friend class Session;

  QString id_;
  std::vector<QString> aliases_;
  std::experimental::optional<QString> name_;
};

}

#endif
