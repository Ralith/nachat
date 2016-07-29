#include "MainWindow.hpp"
#include "ui_MainWindow.h"

#include <algorithm>

#include <QSettings>
#include <QProgressBar>
#include <QLabel>
#include <QSystemTrayIcon>

#include "matrix/Room.hpp"
#include "matrix/Session.hpp"

#include "sort.hpp"
#include "RoomView.hpp"
#include "ChatWindow.hpp"

MainWindow::MainWindow(QSettings &settings, std::unique_ptr<matrix::Session> session)
    : ui(new Ui::MainWindow), settings_(settings), session_(std::move(session)),
      progress_(new QProgressBar(this)), sync_label_(new QLabel(this)) {
  ui->setupUi(this);

  ui->status_bar->addPermanentWidget(sync_label_);
  ui->status_bar->addPermanentWidget(progress_);

  auto tray = new QSystemTrayIcon(QIcon::fromTheme("user-available"), this);
  tray->setContextMenu(ui->menu_matrix);
  connect(tray, &QSystemTrayIcon::activated, [this](QSystemTrayIcon::ActivationReason reason) {
      if(reason == QSystemTrayIcon::Trigger) {
        setVisible(!isVisible());
      }
    });
  tray->show();

  connect(ui->action_log_out, &QAction::triggered, session_.get(), &matrix::Session::log_out);
  connect(session_.get(), &matrix::Session::logged_out, [this]() {
      settings_.remove("session/access_token");
      settings_.remove("session/user_id");
      ui->action_quit->trigger();
    });

  connect(session_.get(), &matrix::Session::error, [this](QString msg) {
      qDebug() << "Session error: " << msg;
    });

  connect(session_.get(), &matrix::Session::synced_changed, [this]() {
      if(session_->synced()) {
        sync_label_->hide();
      } else {
        sync_label_->setText(tr("Disconnected"));
        sync_label_->show();
      }
    });

  connect(session_.get(), &matrix::Session::sync_progress, this, &MainWindow::sync_progress);
  connect(session_.get(), &matrix::Session::sync_complete, [this]() {
      progress_->hide();
      sync_label_->hide();
    });
  connect(session_.get(), &matrix::Session::joined, this, &MainWindow::joined);

  ui->action_quit->setShortcuts(QKeySequence::Quit);
  connect(ui->action_quit, &QAction::triggered, this, &MainWindow::quit);

  connect(ui->room_list, &QListWidget::itemActivated, [this](QListWidgetItem *){
      for(auto item : ui->room_list->selectedItems()) {
        auto &room = *reinterpret_cast<matrix::Room *>(item->data(Qt::UserRole).value<void*>());
        ChatWindow *window;
        auto it = chat_windows_.find(room.id());
        if(it != chat_windows_.end()) {
          window = it->second;   // Focus in existing window
        } else if(last_focused_) {
          window = last_focused_; // Add to most recently used window
        } else {
          if(chat_windows_.empty()) {
            // Create first window
            window = spawn_chat_window();
          } else {
            // Select arbitrary window
            window = chat_windows_.begin()->second;
          }
        }
        window->add_or_focus(room);
        window->show();
        window->raise();
        window->activateWindow();
      }
    });

  sync_progress(0, -1);
  for(auto room : session_->rooms()) {
    joined(*room);
  }
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::joined(matrix::Room &room) {
  connect(&room, &matrix::Room::highlight_count_changed, [this, &room](uint64_t old) {
      highlighted(room, old);
    });
  connect(&room, &matrix::Room::notification_count_changed, this, &MainWindow::update_rooms);
  connect(&room, &matrix::Room::name_changed, this, &MainWindow::update_rooms);
  connect(&room, &matrix::Room::canonical_alias_changed, this, &MainWindow::update_rooms);
  connect(&room, &matrix::Room::aliases_changed, this, &MainWindow::update_rooms);
  connect(&room, &matrix::Room::membership_changed, this, &MainWindow::update_rooms);
  update_rooms();
}

void MainWindow::highlighted(matrix::Room &room, uint64_t old) {
  update_rooms();
  if(old > room.highlight_count()) return;
  auto it = chat_windows_.find(room.id());
  QWidget *window;
  if(it == chat_windows_.end()) {
    window = this;
  } else {
    window = it->second;
  }
  window->show();
  QApplication::alert(window);
}

void MainWindow::update_rooms() {
  auto rooms = session_->rooms();
  std::sort(rooms.begin(), rooms.end(),
            [&](const matrix::Room *a, const matrix::Room *b) {
              return room_sort_key(a->pretty_name()) < room_sort_key(b->pretty_name());
            });
  ui->room_list->clear();
  for(auto room : rooms) {
    auto item = new QListWidgetItem;
    item->setText(room->pretty_name_highlights());
    {
      auto f = font();
      f.setBold(room->highlight_count() != 0 || room->notification_count() != 0);
      item->setFont(f);
    }
    item->setData(Qt::UserRole, QVariant::fromValue(reinterpret_cast<void*>(room)));
    ui->room_list->addItem(item);
  }
  ui->room_list->viewport()->update();
}

void MainWindow::sync_progress(qint64 received, qint64 total) {
  sync_label_->setText(tr("Synchronizing..."));
  sync_label_->show();
  progress_->show();
  if(total == -1 || total == 0) {
    progress_->setMaximum(0);
  } else {
    progress_->setMaximum(1000);
    progress_->setValue(1000 * static_cast<float>(received)/static_cast<float>(total));
  }
}

RoomWindowBridge::RoomWindowBridge(matrix::Room &room, ChatWindow &parent) : QObject(&parent), room_(room), window_(parent) {
  connect(&room, &matrix::Room::highlight_count_changed, this, &RoomWindowBridge::display_changed);
  connect(&room, &matrix::Room::notification_count_changed, this, &RoomWindowBridge::display_changed);
  connect(&room, &matrix::Room::name_changed, this, &RoomWindowBridge::display_changed);
  connect(&room, &matrix::Room::canonical_alias_changed, this, &RoomWindowBridge::display_changed);
  connect(&room, &matrix::Room::aliases_changed, this, &RoomWindowBridge::display_changed);
  connect(&room, &matrix::Room::membership_changed, this, &RoomWindowBridge::display_changed);
  connect(&parent, &ChatWindow::released, this, &RoomWindowBridge::check_release);
}

void RoomWindowBridge::display_changed() {
  window_.room_display_changed(room_);
}

void RoomWindowBridge::check_release(const matrix::RoomID &room) {
  if(room_.id() == room) deleteLater();
}

ChatWindow *MainWindow::spawn_chat_window() {
  auto window = new ChatWindow;
  connect(window, &ChatWindow::focused, [this, window](){
      last_focused_ = window;
    });
  connect(window, &ChatWindow::claimed, [this, window](const matrix::RoomID &r) {
      auto x = chat_windows_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(r),
        std::forward_as_tuple(window));
      assert(x.second);
      new RoomWindowBridge(*session_->room_from_id(r), *window);
    });
  connect(window, &ChatWindow::released, [this](const matrix::RoomID &rid) {
      chat_windows_.erase(rid);
    });
  connect(window, &ChatWindow::pop_out, [this](const matrix::RoomID &r, RoomView *v) {
      auto w = spawn_chat_window();
      w->add(*session_->room_from_id(r), v);
      w->show();
      w->raise();
      w->activateWindow();
    });
  return window;
}
