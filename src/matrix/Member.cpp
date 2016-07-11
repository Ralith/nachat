#include "Member.hpp"

#include <QDebug>

#include "proto.hpp"

namespace matrix {

std::experimental::optional<Membership> parse_membership(const QString &m) {
  std::pair<QString, Membership> table[] = {
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

void Member::update_membership(const QJsonObject &content) {
  auto membership = parse_membership(content["membership"].toString());
  if(!membership) {
    qDebug() << "Unrecognized membership type" << content["membership"].toString();
  } else {
    membership_ = *membership;
  }

  auto i = content.find("displayname");
  if(i != content.end()) {
    display_name_ = i->toString();
  }
  i = content.find("avatar_url");
  if(i != content.end()) {
    auto str = i->toString();
    if(str == "") {
      avatar_url_ = QUrl();
    } else {
      avatar_url_ = QUrl(str);
    }
  }
}

}
