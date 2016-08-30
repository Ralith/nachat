#include "MemberList.hpp"

#include <QScrollBar>

#include "matrix/Room.hpp"

QString MemberList::Compare::key(const QString &n) {
  int i = 0;
  while((n[i] == '@') && (i < n.size())) {
    ++i;
  }
  if(i == n.size()) return n.toCaseFolded();
  return QString(n.data() + i, n.size() - i).toCaseFolded();
}

MemberList::MemberList(const matrix::RoomState &s, QWidget *parent) : QListWidget(parent) {
  auto initial_members = s.members();
  for(const auto &member : initial_members) {
    members_.emplace(s.member_name(member->first), member->first);
  }
  update_members();
}

void MemberList::member_changed(const matrix::RoomState &state, const matrix::UserID &id,
                                const matrix::event::room::MemberContent &old, const matrix::event::room::MemberContent &current) {
  if(membership_displayable(old.membership()) && old.displayname() != current.displayname()) {
    members_.erase(state.member_name(id));
  }
  if(membership_displayable(current.membership())) {
    if(current.displayname()) {
      auto dis = state.nonmember_disambiguation(id, *current.displayname());
      members_.emplace(*current.displayname() + (dis ? " (" + *dis + ")" : ""), id);
    } else {
      members_.emplace(id.value(), id);
    }
  }
  update_members();
}

void MemberList::member_disambiguation_changed(const matrix::RoomState &state, const matrix::UserID &id,
                                               const std::experimental::optional<QString> &old, const std::experimental::optional<QString> &current) {
  (void)old;
  const auto &dn = *state.member_from_id(id)->displayname();
  members_.erase(state.member_name(id));
  members_.emplace(dn + (current ? " (" + *current + ")" : ""), id);
  update_members();
}

void MemberList::update_members() {
  clear();
  for(const auto &member : members_) {
    auto item = new QListWidgetItem;
    item->setText(member.first);
    item->setToolTip(member.second.value());
    item->setData(Qt::UserRole, member.second.value());
    addItem(item);
  }
  setVisible(count() > 2);
  auto margins = contentsMargins();
  size_hint_ = QSize(sizeHintForColumn(0) + verticalScrollBar()->sizeHint().width() + margins.left() + margins.right(),
                     fontMetrics().lineSpacing() + horizontalScrollBar()->sizeHint().height());
  updateGeometry();
}

QSize MemberList::sizeHint() const {
  return size_hint_;
}
