#ifndef NACHAT_JOINED_ROOM_LIST_MODEL_HPP_
#define NACHAT_JOINED_ROOM_LIST_MODEL_HPP_

#include <vector>
#include <unordered_map>
#include <experimental/optional>

#include <QAbstractListModel>
#include <QUrl>
#include <QPixmap>

#include "matrix/ID.hpp"

namespace matrix {
class Room;
class Session;
}

struct RoomInfo {
  matrix::RoomID id;
  QString display_name;
  bool unread;
  std::size_t highlight_count;
  QUrl avatar_url;
  std::experimental::optional<QPixmap> avatar;
  std::size_t avatar_generation;

  explicit RoomInfo(const matrix::Room &);
  void update(const matrix::Room &);
};

class JoinedRoomListModel : public QAbstractListModel {
  Q_OBJECT

public:
  enum Role {
    IDRole = Qt::UserRole, UnreadRole
  };

  explicit JoinedRoomListModel(matrix::Session &session, QSize icon_size, qreal device_pixel_ratio);

  int rowCount(const QModelIndex &parent) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

  void icon_size_changed(const QSize &size);

private:
  matrix::Session &session_;
  std::vector<RoomInfo> rooms_;
  std::unordered_map<matrix::RoomID, std::size_t> index_;
  QSize icon_size_;
  qreal device_pixel_ratio_;

  void joined(matrix::Room &room);
  void update_room(matrix::Room &room);

  void update_avatar(RoomInfo &);
};

#endif
