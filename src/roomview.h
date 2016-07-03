#ifndef ROOMVIEW_H
#define ROOMVIEW_H

#include <QWidget>

namespace Ui {
class RoomView;
}

namespace matrix {
class Room;
class User;
}

class RoomView : public QWidget
{
  Q_OBJECT

public:
  explicit RoomView(matrix::Room &room, QWidget *parent = 0);
  ~RoomView();

private:
  Ui::RoomView *ui;
  matrix::Room &room_;

  void fit_text();
  void update_users();
};

#endif // ROOMVIEW_H
