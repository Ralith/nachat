#ifndef CHATWINDOW_H
#define CHATWINDOW_H

#include <unordered_set>

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

  void add_or_focus_view(RoomView &view);

private:
  Ui::ChatWindow *ui;

  std::unordered_set<RoomView *> tabs_;

  void update_labels();
  void tab_selected(int);
};

#endif // CHATWINDOW_H
