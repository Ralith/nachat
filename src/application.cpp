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
  auto homeserver = settings_.value("login/homeserver");
  auto access_token = settings_.value("session/access_token");
  auto user_id = settings_.value("session/user_id");
  if(access_token.isNull() || homeserver.isNull() || user_id.isNull()) {
    login_.show();
  } else {
    main_window_ = std::make_unique<MainWindow>(
      settings_,
      std::make_unique<matrix::Session>(m_, homeserver.toString(), user_id.toString(), access_token.toString()));
    main_window_->show();
  }
}
