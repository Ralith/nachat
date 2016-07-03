#ifndef CHATWINDOW_H
#define CHATWINDOW_H

#include <QMainWindow>

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

  void add_room(matrix::Room &room);

private:
  Ui::ChatWindow *ui;
};

#endif // CHATWINDOW_H
