#include "MainWindow.hpp"
#include "ui_MainWindow.h"

#include <algorithm>
#include <unordered_set>

#include <QProgressBar>
#include <QLabel>
#include <QSystemTrayIcon>
#include <QDebug>
#include <QMimeDatabase>

#include "matrix/Room.hpp"
#include "matrix/Session.hpp"

#include "sort.hpp"
#include "RoomView.hpp"
#include "ChatWindow.hpp"
#include "JoinDialog.hpp"
#include "MessageBox.hpp"

MainWindow::MainWindow(matrix::Session &session)
    : ui(new Ui::MainWindow), session_(session),
      progress_(new QProgressBar(this)), sync_label_(new QLabel(this)),
      thumbnail_cache_{devicePixelRatioF()} {
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

  connect(ui->action_log_out, &QAction::triggered, this, &MainWindow::log_out);

  connect(ui->action_join, &QAction::triggered, [this]() {
      QPointer<JoinDialog> dialog(new JoinDialog);
      dialog->setAttribute(Qt::WA_DeleteOnClose);
      connect(dialog, &QDialog::accepted, [this, dialog]() {
          const QString room = dialog->room();
          auto reply = session_.join(room);
          connect(reply, &matrix::JoinRequest::error, [room, dialog](const QString &msg) {
              if(!dialog) return;
              dialog->setEnabled(true);
              MessageBox::critical(tr("Failed to join room"), tr("Couldn't join %1: %2").arg(room).arg(msg), dialog);
            });
          connect(reply, &matrix::JoinRequest::success, dialog, &QWidget::close);
        });
      dialog->open();
    });

  connect(&session_, &matrix::Session::error, [this](QString msg) {
      qDebug() << "Session error: " << msg;
    });

  connect(&session_, &matrix::Session::synced_changed, [this]() {
      if(session_.synced()) {
        sync_label_->hide();
      } else {
        sync_label_->setText(tr("Disconnected"));
        sync_label_->show();
      }
    });

  connect(&session_, &matrix::Session::sync_progress, this, &MainWindow::sync_progress);
  connect(&session_, &matrix::Session::sync_complete, [this]() {
      progress_->hide();
      sync_label_->hide();
    });
  connect(&session_, &matrix::Session::joined, this, &MainWindow::joined);

  ui->action_quit->setShortcuts(QKeySequence::Quit);
  connect(ui->action_quit, &QAction::triggered, this, &MainWindow::quit);

  connect(ui->room_list, &QListWidget::itemActivated, [this](QListWidgetItem *){
      std::unordered_set<ChatWindow *> windows;
      for(auto item : ui->room_list->selectedItems()) {
        auto &room = *reinterpret_cast<matrix::Room *>(item->data(Qt::UserRole).value<void*>());
        ChatWindow *window = nullptr;
        auto &i = rooms_.at(room.id());
        if(i.window) {
          window = i.window;   // Focus in existing window
        } else if(last_focused_) {
          window = last_focused_; // Add to most recently used window
        } else {
          // Select arbitrary window
          for(auto &j : rooms_) {
            if(j.second.window) {
              window = j.second.window;
              break;
            }
          }
          if(!window) {
            // Create first window
            window = spawn_chat_window();
          }
        }
        window->add_or_focus(room);
        windows.insert(window);
      }
      for(auto window : windows) {
        window->show();
        window->activateWindow();
      }
    });

  connect(&thumbnail_cache_, &ThumbnailCache::needs, [this](const matrix::Thumbnail &t) {
      auto fetch = session_.get_thumbnail(t);
      QPointer<MainWindow> self(this);
      connect(fetch, &matrix::ContentFetch::finished, [=](const QString &type, const QString &disposition, const QByteArray &data) {
          (void)disposition;
          QPixmap pixmap;
          pixmap.loadFromData(data, QMimeDatabase().mimeTypeForName(type.toUtf8()).preferredSuffix().toUtf8().constData());
          if(pixmap.isNull()) pixmap.loadFromData(data);

          if(pixmap.width() > t.size().width() || pixmap.height() > t.size().height())
            pixmap = pixmap.scaled(t.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
          pixmap.setDevicePixelRatio(self->devicePixelRatioF());

          self->thumbnail_cache_.set(t, pixmap);
        });
    });

  sync_progress(0, -1);
  for(auto room : session_.rooms()) {
    joined(*room);
  }
}

MainWindow::~MainWindow() {
  std::unordered_set<ChatWindow *> windows;
  for(auto &room : rooms_) {
    windows.insert(room.second.window);
  }
  for(auto window : windows) {
    delete window;
  }
  delete ui;
}

void MainWindow::joined(matrix::Room &room) {
  auto &i = rooms_.emplace(
    std::piecewise_construct,
    std::forward_as_tuple(room.id()),
    std::forward_as_tuple()).first->second;
  i.item = new QListWidgetItem;
  i.item->setData(Qt::UserRole, QVariant::fromValue(reinterpret_cast<void*>(&room)));
  // TODO: Sorting
  ui->room_list->addItem(i.item);
  i.display_name = room.pretty_name_highlights();
  i.highlight_count = room.highlight_count() + room.notification_count();
  i.has_unread = room.has_unread();
  update_room(i);

  connect(&room, &matrix::Room::highlight_count_changed, [this, &room](uint64_t old) {
      update_room(room);
      if(old <= room.highlight_count()) {
        highlight(room.id());
      }
    });
  connect(&room, &matrix::Room::notification_count_changed, [this, &room](uint64_t old) {
      update_room(room);
      if(old <= room.notification_count()) {
        highlight(room.id());
      }
    });
  connect(&room, &matrix::Room::sync_complete, [this, &room]() { update_room(room); });
}

void MainWindow::highlight(const matrix::RoomID &room) {
  QWidget *window = rooms_.at(room).window;
  if(!window) {
    window = this;
  }
  window->show();
  QApplication::alert(window);
}

void MainWindow::update_room(matrix::Room &room) {
  auto &i = rooms_.at(room.id());
  i.display_name = room.pretty_name_highlights();
  i.highlight_count = room.highlight_count() + room.notification_count();
  i.has_unread = room.has_unread();
  update_room(i);
}

void MainWindow::update_room(RoomInfo &info) {
  info.item->setText(info.display_name);
  auto f = font();
  f.setBold(info.highlight_count != 0 || info.has_unread);
  info.item->setFont(f);
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
  connect(&room, &matrix::Room::sync_complete, this, &RoomWindowBridge::display_changed);
  connect(&parent, &ChatWindow::released, this, &RoomWindowBridge::check_release);
}

void RoomWindowBridge::display_changed() {
  window_.room_display_changed(room_);
}

void RoomWindowBridge::check_release(const matrix::RoomID &room) {
  if(room_.id() == room) deleteLater();
}

ChatWindow *MainWindow::spawn_chat_window() {
  // We don't create these as children to prevent Qt from hinting to WMs that they should be floating
  auto window = new ChatWindow(thumbnail_cache_);
  connect(window, &ChatWindow::focused, [this, window]() {
      last_focused_ = window;
    });
  connect(window, &ChatWindow::claimed, [this, window](const matrix::RoomID &r) {
      rooms_.at(r).window = window;
      new RoomWindowBridge(*session_.room_from_id(r), *window);
    });
  connect(window, &ChatWindow::released, [this](const matrix::RoomID &rid) {
      rooms_.at(rid).window = nullptr;
    });
  connect(window, &ChatWindow::pop_out, [this](const matrix::RoomID &r, RoomView *v) {
      auto w = spawn_chat_window();
      w->add(*session_.room_from_id(r), v);
      w->show();
      w->raise();
      w->activateWindow();
    });
  return window;
}
