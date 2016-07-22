#ifndef NATIVE_CHAT_ROOM_VIEW_LIST_HPP_
#define NATIVE_CHAT_ROOM_VIEW_LIST_HPP_

#include <QListWidget>
#include <unordered_map>

class QMenu;

namespace matrix {
class Room;
}

class RoomViewList : public QListWidget {
  Q_OBJECT
public:
  RoomViewList(QWidget *parent = nullptr);

  void add(matrix::Room &room);
  void release(matrix::Room &room);
  void activate(matrix::Room &room);
  void update_name(matrix::Room &room);

signals:
  void released(matrix::Room &);
  void claimed(matrix::Room &);
  void activated(matrix::Room &);

protected:
  void contextMenuEvent(QContextMenuEvent *) override;

private:
  std::unordered_map<matrix::Room *, QListWidgetItem *> items_;
  QMenu *menu_;
  matrix::Room *context_;
};

#endif
