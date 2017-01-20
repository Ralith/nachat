#include "MemberListModel.hpp"

#include <experimental/optional>

#include "Room.hpp"

using std::experimental::optional;

namespace matrix {

MemberListModel::Info::Info(UserID id, event::room::MemberContent content, optional<QString> disambiguation) :
  id{id}, content{content}, disambiguation{disambiguation}
{}

MemberListModel::MemberListModel(Room &room, QObject *parent) : QAbstractListModel{parent}, room_{room} {
  connect(&room, &Room::member_changed, this, &MemberListModel::member_changed);
  connect(&room, &Room::member_disambiguation_changed, this, &MemberListModel::member_disambiguation_changed);

  auto members = room.state().members();
  beginInsertRows(QModelIndex(), 0, members.size()-1);
  members_.reserve(members.size());
  for(auto member: members) {
    index_.emplace(member->first, members_.size());
    members_.emplace_back(member->first, member->second, room_.state().member_disambiguation(member->first));
  }
  endInsertRows();
}

int MemberListModel::rowCount(const QModelIndex &parent) const {
  if(parent != QModelIndex()) return 0;
  return members_.size();
}

QVariant MemberListModel::data(const QModelIndex &index, int role) const {
  if(index.column() != 0 || static_cast<size_t>(index.row()) >= members_.size()) {
    return QVariant();
  }
  const auto &info = members_[index.row()];
  switch(role) {
  case Qt::DisplayRole:
    return pretty_name(info.id, info.content);
  case Qt::ToolTipRole:
  case IDRole:
    return info.id.value();
  default:
    return QVariant();
  }
}

QVariant MemberListModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if(role != Qt::DisplayRole || section != 0) {
    return QVariant();
  }

  if(orientation == Qt::Horizontal) {
    return tr("Member");
  }

  return QVariant();
}

void MemberListModel::member_changed(const UserID &id, const event::room::MemberContent &old, const event::room::MemberContent &current) {
  switch(old.membership()) {
  case Membership::LEAVE:
  case Membership::BAN:
    switch(current.membership()) {
    case Membership::JOIN:
    case Membership::INVITE:
      beginInsertRows(QModelIndex(), members_.size(), members_.size());
      index_.emplace(id, members_.size());
      members_.emplace_back(id, current, room_.state().nonmember_disambiguation(id, current.displayname()));
      endInsertRows();
      break;

    case Membership::LEAVE:
    case Membership::BAN:
      break;
    }
    break;

  case Membership::JOIN:
  case Membership::INVITE:
    switch(current.membership()) {
    case Membership::LEAVE:
    case Membership::BAN: {
      auto it = index_.find(id);
      beginRemoveRows(QModelIndex(), it->second, it->second);
      if(it->second != members_.size() - 1) {
        layoutAboutToBeChanged();
        auto &swap_target = members_[it->second];
        std::swap(swap_target, members_.back());
        std::swap(index_.at(swap_target.id), it->second);
        changePersistentIndex(index(members_.size()-1), index(it->second));
        layoutChanged();
      }
      members_.pop_back();
      index_.erase(it);
      endRemoveRows();
      break;
    }

    case Membership::JOIN:
    case Membership::INVITE: {
      std::size_t i = index_.at(id);
      members_[i].content = current;
      dataChanged(index(i), index(i));
      break;
    }
    }
    break;
  }
}

void MemberListModel::member_disambiguation_changed(const UserID &id, const optional<QString> &old, const optional<QString> &current) {
  (void)old;
  std::size_t i = index_.at(id);
  members_[i].disambiguation = current;
  dataChanged(index(i), index(i));
}

}
