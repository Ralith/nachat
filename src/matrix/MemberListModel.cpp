#include "MemberListModel.hpp"

#include <experimental/optional>

#include <QPointer>
#include <QDebug>

#include "Room.hpp"
#include "Session.hpp"
#include "pixmaps.hpp"

using std::experimental::optional;

namespace matrix {

MemberListModel::Info::Info(UserID id, event::room::MemberContent content, optional<QString> disambiguation) :
  id{id}, content{content}, disambiguation{disambiguation}
{}

MemberListModel::MemberListModel(Room &room, QSize icon_size, qreal device_pixel_ratio, QObject *parent) : QAbstractListModel{parent}, room_{room}, icon_size_{icon_size}, device_pixel_ratio_{device_pixel_ratio} {
  connect(&room, &Room::member_changed, this, &MemberListModel::member_changed);
  connect(&room, &Room::member_disambiguation_changed, this, &MemberListModel::member_disambiguation_changed);

  auto members = room.state().members();
  beginInsertRows(QModelIndex(), 0, members.size()-1);
  members_.reserve(members.size());
  for(auto member: members) {
    index_.emplace(member->first, members_.size());
    members_.emplace_back(member->first, member->second, room_.state().member_disambiguation(member->first));
    queue_fetch(members_.back());
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
  case Qt::EditRole:
    return pretty_name(info.id, info.content);
  case Qt::ToolTipRole:
  case IDRole:
    return info.id.value();
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

QVariant MemberListModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if(role != Qt::DisplayRole || section != 0) {
    return QVariant();
  }

  if(orientation == Qt::Horizontal) {
    return tr("Member");
  }

  return QVariant();
}

void MemberListModel::member_changed(const UserID &id, const event::room::MemberContent &current, const event::room::MemberContent &next) {
  switch(current.membership()) {
  case Membership::LEAVE:
  case Membership::BAN:
    switch(next.membership()) {
    case Membership::JOIN:
    case Membership::INVITE:
      beginInsertRows(QModelIndex(), members_.size(), members_.size());
      index_.emplace(id, members_.size());
      members_.emplace_back(id, next, room_.state().nonmember_disambiguation(id, next.displayname()));
      endInsertRows();
      break;

    case Membership::LEAVE:
    case Membership::BAN:
      break;
    }
    break;

  case Membership::JOIN:
  case Membership::INVITE:
    switch(next.membership()) {
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
      bool update_avatar = members_[i].content.avatar_url() != next.avatar_url();
      members_[i].content = next;
      members_[i].disambiguation = room_.state().member_disambiguation(id);
      dataChanged(index(i), index(i));
      if(update_avatar) {
        queue_fetch(members_[i]);
      }
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

// TODO: Replace queue with view-dependent fetching
void MemberListModel::queue_fetch(const Info &info) {
  QUrl url(info.content.avatar_url().value_or(QString()), QUrl::StrictMode);
  if(!url.isValid()) return;

  bool first_fetch = avatar_fetch_queue_.empty();
  avatar_fetch_queue_[info.id] = url;
  if(first_fetch) do_fetch();
}

void MemberListModel::do_fetch() {
  auto it = avatar_fetch_queue_.begin();
  UserID id = it->first;
  QUrl url = it->second;

  try {
    auto thumbnail = matrix::Thumbnail{matrix::Content{url}, icon_size_ * device_pixel_ratio_, matrix::ThumbnailMethod::SCALE};
    auto fetch = room_.session().get_thumbnail(thumbnail);
    QPointer<MemberListModel> self(this);
    connect(fetch, &matrix::ContentFetch::finished, [=](const QString &type, const QString &disposition, const QByteArray &data) {
        (void)disposition;
        if(!self) return;

        auto it = self->index_.find(id);
        if(it == self->index_.end()) return;
        
        QPixmap pixmap = decode(type, data);

        if(pixmap.width() > thumbnail.size().width() || pixmap.height() > thumbnail.size().height())
          pixmap = pixmap.scaled(thumbnail.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        pixmap.setDevicePixelRatio(self->device_pixel_ratio_);

        self->members_[it->second].avatar = std::move(pixmap);
        self->dataChanged(index(it->second), index(it->second));
        self->finish_fetch(id, url);
      });
    connect(fetch, &matrix::ContentFetch::error, [=](const QString &msg) {
        if(!self) return;
        self->finish_fetch(id, url);
      });
  } catch(matrix::illegal_content_scheme &e) {
    qDebug() << "ignoring avatar with illegal scheme" << url.scheme() << "for user" << id.value();
  }
}

void MemberListModel::finish_fetch(UserID id, QUrl url) {
  auto queue_it = avatar_fetch_queue_.find(id);
  if(queue_it->second == url) {
    avatar_fetch_queue_.erase(queue_it);
  }
  if(!avatar_fetch_queue_.empty()) {
    do_fetch();
  }
}

}
