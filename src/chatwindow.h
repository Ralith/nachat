#ifndef CHATWINDOW_H
#define CHATWINDOW_H

#include <unordered_map>

#include <QMainWindow>

class RoomView;

namespace matrix {
class Room;
}

namespace Ui {
class ChatWindow;
}

class ChatWindow : public QMainWindow
{
  Q_OBJECT

public:
  explicit ChatWindow(QWidget *parent = 0);
  ~ChatWindow();

  void add_or_focus_room(matrix::Room &room);

private:
  Ui::ChatWindow *ui;

  std::unordered_map<matrix::Room *, RoomView *> tabs_;

  void update_label(matrix::Room &, RoomView &);
  void tab_selected(int);
};

#endif // CHATWINDOW_H
