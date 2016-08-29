#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <unordered_map>

#include <QMainWindow>
#include <QPointer>

#include "matrix/Matrix.hpp"
#include "matrix/ID.hpp"

#include "ContentCache.hpp"

class QProgressBar;
class QLabel;
class ChatWindow;
class QListWidgetItem;

namespace Ui {
class MainWindow;
}

namespace matrix {
class Room;
class Session;
}

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(matrix::Session &session);
  ~MainWindow();

signals:
  void quit();
  void log_out();

private:
  struct RoomInfo {
    ChatWindow *window = nullptr;
    QListWidgetItem *item = nullptr;
    bool has_unread = false;
    QString display_name;
    size_t highlight_count = 0;
  };

  Ui::MainWindow *ui;
  matrix::Session &session_;
  QProgressBar *progress_;
  QLabel *sync_label_;
  QPointer<ChatWindow> last_focused_;
  ThumbnailCache thumbnail_cache_;

  std::unordered_map<matrix::RoomID, RoomInfo> rooms_;

  void joined(matrix::Room &room);
  void highlight(const matrix::RoomID &room);
  void update_room(RoomInfo &info);
  void update_room(matrix::Room &room);
  void sync_progress(qint64 received, qint64 total);
  ChatWindow *spawn_chat_window();
};

class RoomWindowBridge : public QObject {
  Q_OBJECT
public:
  RoomWindowBridge(matrix::Room &room, ChatWindow &parent);

  void display_changed();
  void check_release(const matrix::RoomID &room);

private:
  matrix::Room &room_;
  ChatWindow &window_;
};

#endif // MAINWINDOW_H
