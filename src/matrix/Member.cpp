#include "Member.hpp"

#include <QDebug>
#include <QJsonObject>

#include "proto.hpp"

namespace matrix {

std::experimental::optional<Membership> parse_membership(const QString &m) {
  static const std::pair<QString, Membership> table[] = {
    {"invite", Membership::INVITE},
    {"join", Membership::JOIN},
    {"leave", Membership::LEAVE},
    {"ban", Membership::BAN}
  };
  for(const auto &x : table) {
    if(x.first == m) return x.second;
  }
  return {};
}

QString to_qstring(Membership m) {
  switch(m) {
  case Membership::INVITE: return "invite";
  case Membership::JOIN: return "join";
  case Membership::LEAVE: return "leave";
  case Membership::BAN: return "ban";
  }
}

Member::Member(UserID id, event::room::MemberContent content)
  : id_(std::move(id)), member_(content)
{}

}
