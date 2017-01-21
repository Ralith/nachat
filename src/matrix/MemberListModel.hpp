#ifndef NACHAT_MATRIX_MEMBER_LIST_MODEL_HPP_
#define NACHAT_MATRIX_MEMBER_LIST_MODEL_HPP_

#include <vector>
#include <unordered_map>
#include <experimental/optional>

#include <QAbstractListModel>
#include <QPixmap>

#include "ID.hpp"
#include "Event.hpp"

namespace matrix {
class Room;
class ContentFetch;

class MemberListModel : public QAbstractListModel {
public:
  static constexpr int IDRole = Qt::UserRole;

  explicit MemberListModel(Room &room, QSize icon_size, qreal device_pixel_ratio, QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
  struct Info {
    UserID id;
    event::room::MemberContent content;
    std::experimental::optional<QString> disambiguation;
    std::size_t avatar_generation;
    std::experimental::optional<QPixmap> avatar;

    Info(UserID, event::room::MemberContent, std::experimental::optional<QString>);
  };

  Room &room_;
  std::vector<Info> members_;
  std::unordered_map<UserID, std::size_t> index_;
  QSize icon_size_;
  qreal device_pixel_ratio_;

  void member_changed(const UserID &id, const event::room::MemberContent &old, const event::room::MemberContent &current);
  void member_disambiguation_changed(const UserID &id, const std::experimental::optional<QString> &old, const std::experimental::optional<QString> &current);
  void fetch_avatar(Info &info);
};

}

#endif
