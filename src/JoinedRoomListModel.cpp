#include "JoinedRoomListModel.hpp"

#include "matrix/Session.hpp"
#include "matrix/Room.hpp"

RoomInfo::RoomInfo(const matrix::Room &room) :
  id{room.id()}, display_name{room.pretty_name()}, unread{room.has_unread()}, highlight_count{room.highlight_count() + room.notification_count()}
{}

JoinedRoomListModel::JoinedRoomListModel(matrix::Session &session) {
  connect(&session, &matrix::Session::joined, this, &JoinedRoomListModel::joined);
  for(auto room : session.rooms()) {
    joined(*room);
  }
}

int JoinedRoomListModel::rowCount(const QModelIndex &parent) const {
  if(parent != QModelIndex()) return 0;
  return rooms_.size();
}

QVariant JoinedRoomListModel::data(const QModelIndex &index, int role) const {
  if(index.column() != 0 || static_cast<size_t>(index.row()) >= rooms_.size()) {
    return QVariant();
  }
  const auto &info = rooms_[index.row()];
  switch(role) {
  case Qt::DisplayRole:
    return info.display_name;
  case Qt::ToolTipRole:
  case IDRole:
    return info.id.value();
  default:
    return QVariant();
  }
}

QVariant JoinedRoomListModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if(role != Qt::DisplayRole || section != 0) {
    return QVariant();
  }

  if(orientation == Qt::Horizontal) {
    return tr("Room");
  }

  return QVariant();
}

void JoinedRoomListModel::joined(matrix::Room &room) {
  beginInsertRows(QModelIndex(), rooms_.size(), rooms_.size());
  index_.emplace(room.id(), rooms_.size());
  rooms_.emplace_back(room);
  connect(&room, &matrix::Room::sync_complete, [this, &room]() { update_room(room); });
  endInsertRows();
}

void JoinedRoomListModel::update_room(matrix::Room &room) {
  auto i = index_.at(room.id());
  rooms_[i] = RoomInfo{room};
  dataChanged(index(i), index(i));
}
