#include "Application.hpp"

#include <QMenu>

#include "matrix/Session.hpp"

#include "RoomView.hpp"

Application::Application(int &argc, char **&argv)
    : QApplication(argc, argv)
{
  connect(&login_, &LoginDialog::accepted, [this](){
      main_window_ = std::make_unique<MainWindow>(settings_, login_.session());
      session_start();
    });
}

void Application::start() {
  auto homeserver = settings_.value("login/homeserver");
  auto access_token = settings_.value("session/access_token");
  auto user_id = settings_.value("session/user_id");
  if(access_token.isNull() || homeserver.isNull() || user_id.isNull()) {
    login_.show();
  } else {
    main_window_ = std::make_unique<MainWindow>(
      settings_,
      std::unique_ptr<matrix::Session>(matrix::Session::create(m_, homeserver.toString(), user_id.toString(), access_token.toString())));
    session_start();
  }
}

void Application::session_start() {
  setQuitOnLastWindowClosed(false);
  connect(main_window_.get(), &MainWindow::quit, this, &QApplication::quit);
  main_window_->show();
}
