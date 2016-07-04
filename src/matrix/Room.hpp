#ifndef NATIVE_CHAT_MATRIX_ROOM_H_
#define NATIVE_CHAT_MATRIX_ROOM_H_

#include <vector>
#include <list>
#include <unordered_set>
#include <unordered_map>

#include <QString>
#include <QObject>
#include <QUrl>

#include <span.h>

#include "../QStringHash.hpp"

#include "Member.hpp"

namespace matrix {

class Matrix;

namespace proto {
struct JoinedRoom;
}

struct Message {
  QString body;
};

enum class Membership {
  INVITE, JOIN, LEAVE, BAN
};

class Room : public QObject {
  Q_OBJECT

public:
  Room(Matrix &universe, QString user_id, QString id) : universe_(universe), user_id_(user_id), id_(id) {}

  Room(const Room &) = delete;
  Room &operator=(const Room &) = delete;

  const QString &id() const { return id_; }
  const QString &canonical_alias() const { return canonical_alias_; }
  gsl::span<const QString> aliases() const { return aliases_; }
  const QString &name() const { return name_; }
  uint64_t highlight_count() const { return highlight_count_; }
  uint64_t notification_count() const { return notification_count_; }
  const std::list<Message> &messages() { return messages_; }

  std::vector<const Member *> members() const;
  std::vector<const Member *> members_named(QString displayname) const;
  std::vector<const Message *> messages() const;

  QString pretty_name() const;
  // Matrix r0.1.0 11.2.2.5

  QString member_name(const Member &member) const;
  // Matrix r0.1.0 11.2.2.3

  void dispatch(const proto::JoinedRoom &);

signals:
  void messages_changed();
  void members_changed(gsl::span<const Member *const> new_members);
  void name_changed();
  void aliases_changed();
  void highlight_count_changed();
  void notification_count_changed();
  void pretty_name_changed();  // May be emitted spuriously
  void member_names_changed();

private:
  Matrix &universe_;
  const QString user_id_;
  const QString id_;
  QString canonical_alias_;
  std::vector<QString> aliases_;
  QString name_;
  std::unordered_map<QString, Member, QStringHash> members_by_id_;
  std::unordered_map<QString, std::vector<Member *>, QStringHash> members_by_displayname_;
  std::list<Message> messages_;
  uint64_t highlight_count_ = 0, notification_count_ = 0;

  bool forget_displayname(const Member &member);
};

}

#endif
