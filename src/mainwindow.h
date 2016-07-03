#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <unordered_map>
#include <memory>

#include "matrix/matrix.hpp"

#include "chatwindow.h"

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

private:
  Ui::MainWindow *ui;
  QSettings &settings_;
  std::unique_ptr<matrix::Session> session_;
  QProgressBar *progress_;
  QLabel *sync_label_;

  std::unordered_map<matrix::Room *, ChatWindow> chat_windows_;

  void update_rooms();
  void sync_progress(qint64 received, qint64 total);
};

#endif // MAINWINDOW_H
