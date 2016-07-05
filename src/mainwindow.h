#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <unordered_map>
#include <memory>

#include <QMainWindow>

#include "matrix/matrix.hpp"

#include "chatwindow.h"
#include "roomview.h"

class QProgressBar;
class QSettings;
class QLabel;

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

  std::unordered_map<matrix::Room *, ChatWindow> chat_windows_;
  std::unordered_map<matrix::Room *, RoomView> room_views_;

  void update_rooms();
  void sync_progress(qint64 received, qint64 total);
};

#endif // MAINWINDOW_H
