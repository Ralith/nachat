#ifndef CHATWINDOW_H
#define CHATWINDOW_H

#include <unordered_map>
#include <memory>

#include <QWidget>

class RoomView;

namespace matrix {
class Room;
}

namespace Ui {
class ChatWindow;
}

class ChatWindow : public QWidget
{
  Q_OBJECT

public:
  explicit ChatWindow(QWidget *parent = 0);
  ~ChatWindow();

  void add_or_focus(matrix::Room &);

private:
  Ui::ChatWindow *ui;

  std::unordered_map<matrix::Room *, std::unique_ptr<RoomView>> tabs_;

  void update_labels();
  void tab_selected(int);
};

#endif // CHATWINDOW_H
