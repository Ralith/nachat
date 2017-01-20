#ifndef NACHAT_JOINED_ROOM_LIST_MODEL_HPP_
#define NACHAT_JOINED_ROOM_LIST_MODEL_HPP_

#include <vector>
#include <unordered_map>

#include <QAbstractListModel>

#include "matrix/ID.hpp"

namespace matrix {
class Room;
class Session;
}

struct RoomInfo {
  matrix::RoomID id;
  QString display_name;
  bool unread;
  size_t highlight_count;

  explicit RoomInfo(const matrix::Room &);
};

class JoinedRoomListModel : public QAbstractListModel {
public:
  static constexpr int IDRole = Qt::UserRole;

  explicit JoinedRoomListModel(matrix::Session &session);

  int rowCount(const QModelIndex &parent) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
  std::vector<RoomInfo> rooms_;
  std::unordered_map<matrix::RoomID, std::size_t> index_;

  void joined(matrix::Room &room);
  void update_room(matrix::Room &room);
};

#endif
