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
    members_.insert(std::make_pair(s.member_name(*member), member));
  }
  update_members();
}

void MemberList::member_display_changed(const matrix::RoomState &s, const matrix::Member &m, const QString &old) {
  auto erased = members_.erase(old);
  if(!erased) {
    QString msg = "member name changed from unknown name " + old + " to " + s.member_name(m);
    throw std::logic_error(msg.toStdString().c_str());
  }
  members_.insert(std::make_pair(s.member_name(m), &m));
  update_members();
}

void MemberList::membership_changed(const matrix::RoomState &s, const matrix::Member &m) {
  if(membership_displayable(m.membership())) {
    members_.insert(std::make_pair(s.member_name(m), &m));
  } else {
    members_.erase(s.member_name(m));
  }
  update_members();
}

void MemberList::update_members() {
  clear();
  for(const auto &member : members_) {
    auto item = new QListWidgetItem;
    item->setText(member.first);
    item->setToolTip(member.second->id());
    item->setData(Qt::UserRole, QVariant::fromValue(const_cast<void*>(reinterpret_cast<const void*>(member.second))));
    addItem(item);
  }
  setVisible(count() > 2);
  auto margins = contentsMargins();
  size_hint_ = QSize(sizeHintForColumn(0) + verticalScrollBar()->sizeHint().width() + margins.left() + margins.right(),
                     fontMetrics().lineSpacing() + horizontalScrollBar()->sizeHint().height());
  update();
}

QSize MemberList::sizeHint() const {
  return size_hint_;
}
