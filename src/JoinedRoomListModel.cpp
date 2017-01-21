#include "JoinedRoomListModel.hpp"

#include <QFont>
#include <QDebug>
#include <QPointer>
#include <QMimeDatabase>

#include "matrix/Session.hpp"
#include "matrix/Room.hpp"
#include "matrix/pixmaps.hpp"

RoomInfo::RoomInfo(const matrix::Room &room) :
  id{room.id()}, avatar_generation{0}
{
  update(room);
}

void RoomInfo::update(const matrix::Room &room) {
  assert(id == room.id());
  display_name = room.pretty_name();
  unread = room.has_unread();
  highlight_count = room.highlight_count() + room.notification_count();
  avatar_url = room.state().avatar();
}

JoinedRoomListModel::JoinedRoomListModel(matrix::Session &session, QSize icon_size, qreal dpr) : session_{session}, icon_size_{icon_size}, device_pixel_ratio_{dpr} {
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
  case UnreadRole:
    return info.unread;
  case Qt::FontRole: {
    QFont font;
    font.setBold(info.unread);
    return font;
  }
  case Qt::DecorationRole: {
    if(auto avatar = info.avatar) {
      return *avatar;
    }
    return QVariant();
  }
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

  update_avatar(rooms_.back());
  
}

void JoinedRoomListModel::update_room(matrix::Room &room) {
  auto i = index_.at(room.id());
  rooms_[i].update(room);
  dataChanged(index(i), index(i));
}

void JoinedRoomListModel::update_avatar(RoomInfo &info) {
  if(info.avatar_url.isEmpty()) {
    info.avatar = {};
    return;
  }

  try {
    auto thumbnail = matrix::Thumbnail{matrix::Content{info.avatar_url}, icon_size_ * device_pixel_ratio_, matrix::ThumbnailMethod::SCALE};
    auto fetch = session_.get_thumbnail(thumbnail);
    QPointer<JoinedRoomListModel> self(this);
    matrix::RoomID id = info.id;
    info.avatar_generation += 1;
    std::size_t generation = info.avatar_generation;
    connect(fetch, &matrix::ContentFetch::finished, [=](const QString &type, const QString &disposition, const QByteArray &data) {
        (void)disposition;
        if(!self) return;

        auto it = self->index_.find(id);
        if(it == self->index_.end() || self->rooms_[it->second].avatar_generation != generation) return;
        
        QPixmap pixmap = matrix::decode(type, data);

        if(pixmap.width() > thumbnail.size().width() || pixmap.height() > thumbnail.size().height())
          pixmap = pixmap.scaled(thumbnail.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        pixmap.setDevicePixelRatio(self->device_pixel_ratio_);

        self->rooms_[it->second].avatar = std::move(pixmap);
        self->dataChanged(index(it->second), index(it->second));
      });
  } catch(matrix::illegal_content_scheme &e) {
    qDebug() << "ignoring avatar with illegal scheme for room" << info.display_name;
  }
}

void JoinedRoomListModel::icon_size_changed(const QSize &size) {
  icon_size_ = size;
  for(auto &room: rooms_) {
    update_avatar(room);
  }
}
