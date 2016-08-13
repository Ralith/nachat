#ifndef NATIVE_CHAT_ROOM_VIEW_LIST_HPP_
#define NATIVE_CHAT_ROOM_VIEW_LIST_HPP_

#include <QListWidget>
#include <unordered_map>
#include <experimental/optional>

#include "matrix/ID.hpp"

class QMenu;

namespace matrix {
class Room;
}

class RoomViewList : public QListWidget {
  Q_OBJECT
public:
  RoomViewList(QWidget *parent = nullptr);

  void add(matrix::Room &room);
  void release(const matrix::RoomID &room);
  void activate(const matrix::RoomID &room);
  void update_display(matrix::Room &room);

  QSize sizeHint() const override;

signals:
  void released(const matrix::RoomID &);
  void claimed(const matrix::RoomID &);
  void activated(const matrix::RoomID &);
  void pop_out(const matrix::RoomID &);

protected:
  void contextMenuEvent(QContextMenuEvent *) override;

private:
  struct RoomInfo {
    RoomInfo(QListWidgetItem *i, const matrix::Room &r);

    QListWidgetItem *item;
    bool has_unread;
    QString name;
    size_t highlight_count;
  };

  std::unordered_map<matrix::RoomID, RoomInfo> items_;
  QMenu *menu_;
  std::experimental::optional<matrix::RoomID> context_;

  void update_item(const RoomInfo &i);
};

#endif
