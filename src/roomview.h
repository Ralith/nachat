#ifndef ROOMVIEW_H
#define ROOMVIEW_H

#include <QWidget>

namespace Ui {
class RoomView;
}

namespace matrix {
class Room;
class RoomState;
class Member;
enum class Membership;

struct Message;
}

class RoomView : public QWidget
{
  Q_OBJECT

public:
  explicit RoomView(matrix::Room &room, QWidget *parent = 0);
  ~RoomView();

  const matrix::Room &room() { return room_; }

private:
  Ui::RoomView *ui;
  matrix::Room &room_;

  void update_members();
  void append_message(const matrix::RoomState &, const matrix::Message &);
};

#endif // ROOMVIEW_H
