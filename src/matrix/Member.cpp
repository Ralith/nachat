#include "Member.hpp"

#include <QDebug>
#include <QJsonObject>

#include "proto.hpp"

namespace matrix {

Member::Member(UserID id, event::room::MemberContent content)
  : id_(std::move(id)), member_(content)
{}

}
