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

void Member::dispatch(const proto::Event &state) {
  auto membership = parse_membership(state.content["membership"].toString());
  if(!membership) {
    qDebug() << "Unrecognized membership type" << state.content["membership"].toString();
  } else {
    membership_ = *membership;
  }

  auto i = state.content.find("displayname");
  if(i != state.content.end()) {
    display_name_ = i->toString();
  }
  i = state.content.find("avatar_url");
  if(i != state.content.end()) {
    auto str = i->toString();
    if(str == "") {
      avatar_url_ = QUrl();
    } else {
      avatar_url_ = QUrl(str);
    }
  }
}

}
