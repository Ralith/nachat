#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <algorithm>

#include <QSettings>
#include <QProgressBar>
#include <QLabel>
#include <QSystemTrayIcon>

#include "matrix/Room.hpp"
#include "matrix/Session.hpp"

namespace {

QString room_sort_key(const QString &n) {
  int i = 0;
  while((n[i] == '#' || n[i] == '@') && (i < n.size())) {
    ++i;
  }
  if(i == n.size()) return n.toCaseFolded();
  return QString(n.data() + i, n.size() - i).toCaseFolded();
}

}

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
  connect(session_.get(), &matrix::Session::joined, [this](matrix::Room &room) {
      update_rooms();
    });

  ui->action_quit->setShortcuts(QKeySequence::Quit);
  connect(ui->action_quit, &QAction::triggered, this, &MainWindow::quit);

  connect(ui->room_list, &QListWidget::itemActivated, [this](QListWidgetItem *item){
      auto &room = *reinterpret_cast<matrix::Room *>(item->data(Qt::UserRole).value<void*>());
      auto it = chat_windows_.find(&room);
      if(it == chat_windows_.end()) {
        it = chat_windows_.emplace(
          std::piecewise_construct,
          std::forward_as_tuple(&room),
          std::forward_as_tuple()).first;
      }
      auto &window = it->second;
      window.add_or_focus(room);
      window.show();
      window.activateWindow();
    });

  sync_progress(0, -1);
  update_rooms();
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::update_rooms() {
  // Disconnect signals
  for(int i = 0; i < ui->room_list->count(); ++i) {
    auto &room = *reinterpret_cast<matrix::Room *>(ui->room_list->item(i)->data(Qt::UserRole).value<void*>());
    disconnect(&room, &matrix::Room::state_changed, this, &MainWindow::update_rooms);
  }

  auto rooms = session_->rooms();
  std::sort(rooms.begin(), rooms.end(),
            [&](const matrix::Room *a, const matrix::Room *b) {
              return room_sort_key(a->state().pretty_name(session_->user_id()))
                  < room_sort_key(b->state().pretty_name(session_->user_id()));
            });
  ui->room_list->clear();
  for(auto room : rooms) {
    connect(room, &matrix::Room::state_changed, this, &MainWindow::update_rooms);
    auto item = new QListWidgetItem;
    item->setText(room->pretty_name());
    item->setData(Qt::UserRole, QVariant::fromValue(reinterpret_cast<void*>(room)));
    ui->room_list->addItem(item);
  }
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
