#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include <span.h>

class LabeledProgressBar;

namespace Ui {
class MainWindow;
}

namespace matrix {
class Room;
}

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = 0);
  ~MainWindow();

  void set_rooms(gsl::span<matrix::Room *const>);

  void set_initial_sync(bool underway);

signals:
  void log_out();
  void quit();

private:
  Ui::MainWindow *ui;
  LabeledProgressBar *progress_;
};

#endif // MAINWINDOW_H
