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

  std::unordered_map<matrix::Room *, ChatWindow *> chat_windows_;

  void update_rooms();
  void sync_progress(qint64 received, qint64 total);
  ChatWindow *spawn_chat_window(matrix::Room &);
};

#endif // MAINWINDOW_H
