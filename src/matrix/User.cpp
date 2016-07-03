#include "User.hpp"

#include "proto.hpp"

namespace matrix {

void User::dispatch(const proto::Event &state) {
  auto membership = state.content["membership"].toString();
  bool old_invite_pending = invite_pending_;
  if(membership == "invite") {
    invite_pending_ = true;
  } else if(membership == "join") {
    invite_pending_ = false;
  }
  if(invite_pending_ != old_invite_pending) {
    invite_pending_changed();
  }

  auto i = state.content.find("displayname");
  if(i != state.content.end()) {
    display_name_ = i->toString();
    display_name_changed();
  }
  i = state.content.find("avatar_url");
  if(i != state.content.end()) {
    avatar_url_ = QUrl(i->toString());
    avatar_url_changed();
  }
}

}
