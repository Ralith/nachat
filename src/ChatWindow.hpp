#ifndef NATIVE_CHAT_CHAT_WINDOW_HPP_
#define NATIVE_CHAT_CHAT_WINDOW_HPP_

#include <unordered_map>
#include <memory>

#include <QWidget>

class RoomView;
class QListWidgetItem;

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

signals:
  void focused();
  void released(matrix::Room *);
  void claimed(matrix::Room *);

protected:
  void focusInEvent(QFocusEvent *event) override;
  void closeEvent(QCloseEvent *event) override;

private:
  Ui::ChatWindow *ui;

  struct RoomInfo {
    RoomView *view;
    QListWidgetItem *item;
  };

  std::unordered_map<matrix::Room *, RoomInfo> rooms_;

  void update_title();
  void update_room_list();
  void current_changed(int i);
};

#endif
