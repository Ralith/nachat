#ifndef NATIVE_CHAT_APPLICATION_H_
#define NATIVE_CHAT_APPLICATION_H_

#include <unordered_map>
#include <experimental/optional>

#include <QApplication>

#include "matrix/matrix.hpp"

#include "logindialog.h"
#include "mainwindow.h"
#include "chatwindow.h"

class Application : public QApplication {
  Q_OBJECT

public:
  Application(int argc, char **argv);

  void start();

private:
  // Long-lived state
  QSettings settings_;
  QNetworkAccessManager net_;
  matrix::Matrix m_{net_};
  std::unique_ptr<matrix::Session> session_;

  // UI elements
  LoginDialog login_{m_};
  MainWindow main_window_;
  std::vector<std::unique_ptr<ChatWindow>> chat_windows_;

  void load_session(std::unique_ptr<matrix::Session> s);
  void open_room(matrix::Room &room);
};

#endif
