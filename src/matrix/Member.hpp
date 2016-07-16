#ifndef NATIVE_CHAT_MATRIX_MEMBER_H_
#define NATIVE_CHAT_MATRIX_MEMBER_H_

#include <experimental/optional>

#include <QString>
#include <QUrl>

class QJsonObject;

namespace matrix {

class Room;

namespace proto {
struct Event;
}

enum class Membership {
  INVITE, JOIN, LEAVE, BAN
};

std::experimental::optional<Membership> parse_membership(const QString &m);

// Whether a membership participates in naming per 11.2.2.3
constexpr inline bool membership_displayable(Membership m) {
  return m == Membership::JOIN || m == Membership::INVITE;
}

using MemberID = QString;

class Member {
public:
  Member(QString id) : id_(std::move(id)) {}

  Member(QString id, const QJsonObject &);

  QJsonObject to_json() const;

  const MemberID &id() const { return id_; }
  const QString &display_name() const { return display_name_; }
  const QUrl &avatar_url() const { return avatar_url_; }
  Membership membership() const { return membership_; }

  void update_membership(const QJsonObject &content);

private:
  const MemberID id_;
  QString display_name_;  // Optional
  QUrl avatar_url_;       // Optional
  Membership membership_ = Membership::LEAVE;
};

}

#endif
