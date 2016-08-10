#ifndef NATIVE_CHAT_ROOM_MENU_HPP_
#define NATIVE_CHAT_ROOM_MENU_HPP_

#include <QMenu>

#include "matrix/Room.hpp"

class RoomMenu : public QMenu {
  Q_OBJECT

public:
  RoomMenu(matrix::Room &room, QWidget *parent = nullptr);

private:
  matrix::Room &room_;

  void upload_file(const QString &path);
};

#endif
