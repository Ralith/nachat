#include "application.h"

#include "roomview.h"

Application::Application(int argc, char **argv)
    : QApplication(argc, argv)
{
  connect(&login_, &LoginDialog::accepted, [this](){
      main_window_ = std::make_unique<MainWindow>(settings_, login_.session());
    main_window_->show();
    });
}

void Application::start() {
  auto access_token = settings_.value("login/access_token");
  auto homeserver = settings_.value("login/homeserver");
  if(access_token.isNull() || homeserver.isNull()) {
    login_.show();
  } else {
    main_window_ = std::make_unique<MainWindow>(
      settings_,
      std::make_unique<matrix::Session>(m_, homeserver.toString(), access_token.toString()));
    main_window_->show();
  }
}
