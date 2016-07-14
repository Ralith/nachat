#include "Member.hpp"

#include <QDebug>
#include <QJsonObject>

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

QString to_qstring(Membership m) {
  switch(m) {
  case Membership::INVITE: return "invite";
  case Membership::JOIN: return "join";
  case Membership::LEAVE: return "leave";
  case Membership::BAN: return "ban";
  }
}

Member::Member(QString id, const QJsonObject &o)
    : id_(std::move(id)), display_name_(o["display_name"].toString()), avatar_url_(QUrl(o["avatar_url"].toString())),
      membership_(parse_membership(o["membership"].toString()).value())
{}

QJsonObject Member::to_json() const {
  QJsonObject o;

  if(!display_name_.isNull()) o["display_name"] = display_name_;
  if(!avatar_url_.isEmpty()) o["avatar_url"] = avatar_url_.toString(QUrl::FullyEncoded);
  o["membership"] = to_qstring(membership_);

  return o;
}

void Member::update_membership(const QJsonObject &content) {
  if(content.empty()) {
    // Empty content arises when moving backwards from an initial event
    membership_ = Membership::LEAVE;
    return;
  }

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
