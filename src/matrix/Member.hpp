#ifndef NATIVE_CHAT_MATRIX_MEMBER_H_
#define NATIVE_CHAT_MATRIX_MEMBER_H_

#include <experimental/optional>

#include <QString>
#include <QUrl>

#include "Event.hpp"

class QJsonObject;

namespace matrix {

class Room;

class Member {
public:
  explicit Member(UserID id) : id_(std::move(id)), member_{event::room::MemberContent::leave} {}
  // Can this constructor be removed?

  Member(UserID id, event::room::MemberContent content);

  QJsonObject json() const { return member_.json(); }

  const UserID &id() const { return id_; }
  const std::experimental::optional<QString> &displayname() const { return member_.displayname(); }
  const std::experimental::optional<QString> &avatar_url() const { return member_.avatar_url(); }
  Membership membership() const { return member_.membership(); }
  const QString &pretty_name() const { return member_.displayname() ? *member_.displayname() : id_.value(); }

  void update_membership(event::room::MemberContent content) { member_ = content; }

private:
  UserID id_;
  event::room::MemberContent member_;
};

}

#endif
