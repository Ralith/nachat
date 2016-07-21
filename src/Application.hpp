#ifndef NATIVE_CHAT_APPLICATION_H_
#define NATIVE_CHAT_APPLICATION_H_

#include <unordered_map>
#include <memory>

#include <QtNetwork>
#include <QApplication>

#include "matrix/Matrix.hpp"
#include "matrix/Session.hpp"

#include "LoginDialog.hpp"
#include "MainWindow.hpp"

class Application : public QApplication {
  Q_OBJECT

public:
  Application(int &argc, char **&argv);

  void start();

private:
  // Long-lived state
  QSettings settings_;
  QNetworkAccessManager net_;
  matrix::Matrix m_{net_};

  // UI elements
  LoginDialog login_{m_};
  std::unique_ptr<MainWindow> main_window_;

  void session_start();
};

#endif
