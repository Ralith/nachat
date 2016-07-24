#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <unordered_map>
#include <memory>

#include <QMainWindow>
#include <QPointer>

#include "matrix/Matrix.hpp"

class QProgressBar;
class QSettings;
class QLabel;
class ChatWindow;

namespace Ui {
class MainWindow;
}

namespace matrix {
class Room;
}

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QSettings &settings, std::unique_ptr<matrix::Session> session);
  ~MainWindow();

signals:
  void quit();

private:
  Ui::MainWindow *ui;
  QSettings &settings_;
  std::unique_ptr<matrix::Session> session_;
  QProgressBar *progress_;
  QLabel *sync_label_;
  QPointer<ChatWindow> last_focused_;

  std::unordered_map<matrix::RoomID, ChatWindow *, QStringHash> chat_windows_;

  void joined(matrix::Room &room);
  void highlighted(matrix::Room &room, uint64_t old);
  void update_rooms();
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
