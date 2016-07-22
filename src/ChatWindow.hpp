#ifndef NATIVE_CHAT_CHAT_WINDOW_HPP_
#define NATIVE_CHAT_CHAT_WINDOW_HPP_

#include <unordered_map>
#include <memory>

#include <QWidget>

class RoomView;
class RoomViewList;

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
  void room_name_changed(matrix::Room &);

signals:
  void focused();
  void released(matrix::Room &);
  void claimed(matrix::Room &);

protected:
  void focusInEvent(QFocusEvent *event) override;
  void closeEvent(QCloseEvent *event) override;

private:
  Ui::ChatWindow *ui;
  RoomViewList *room_list_;
  std::unordered_map<matrix::Room *, RoomView *> rooms_;

  void update_title();
  void current_changed(int i);
};

#endif
