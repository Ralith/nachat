#include "application.h"

#include <algorithm>

#include "roomview.h"

Application::Application(int argc, char **argv)
    : QApplication(argc, argv)
{
  connect(&login_, &LoginDialog::accepted, [this](){
      load_session(login_.session());
    });
  connect(&main_window_, &MainWindow::log_out, [this](){
      session_->log_out();
    });
  connect(&main_window_, &MainWindow::quit, [this](){
      quit();
    });

  main_window_.set_initial_sync(true);
}

void Application::start() {
  auto access_token = settings_.value("login/access_token");
  auto homeserver = settings_.value("login/homeserver");
  if(access_token.isNull() || homeserver.isNull()) {
    login_.show();
  } else {
    load_session(std::make_unique<matrix::Session>(net_, homeserver.toString(), access_token.toString()));
  }
}

void Application::load_session(std::unique_ptr<matrix::Session> s) {
  session_ = std::move(s);
  connect(session_.get(), &matrix::Session::logged_out, [this]() {
      settings_.remove("login/access_token");
      quit();
    });
  connect(session_.get(), &matrix::Session::error, [this](QString msg) {
      qDebug() << msg;
    });
  connect(session_.get(), &matrix::Session::synced_changed, [this]() {
      main_window_.set_initial_sync(!session_->synced());
    });
  connect(session_.get(), &matrix::Session::rooms_changed, [this]() {
      auto rooms = session_->rooms();
      main_window_.set_rooms(rooms);
    });
  main_window_.show();
}

void Application::open_room(matrix::Room &room) {
  chat_windows_.emplace_back(new ChatWindow(&main_window_));
  auto &w = *chat_windows_.back();
  w.add_room(room);
  w.show();
}
