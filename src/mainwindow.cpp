#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <algorithm>

#include "matrix/Room.hpp"

#include "LabeledProgressBar.hpp"

namespace {

QString room_sort_key(const matrix::Room &r) {
  const auto &n = r.display_name();
  auto i = std::find_if(n.begin(), n.end(),
                        [](QChar c) { return c != '#'; });
  if(i != n.end()) return n;
  return QString(n.data() + (i - n.begin()), n.end() - i);
}

}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), progress_(new LabeledProgressBar) {
  ui->setupUi(this);

  ui->status_bar->addPermanentWidget(progress_);

  connect(ui->action_log_out, &QAction::triggered, [this]() { log_out(); });

  ui->action_quit->setShortcuts(QKeySequence::Quit);
  connect(ui->action_quit, &QAction::triggered, [this]() { quit(); });
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::set_rooms(gsl::span<matrix::Room *const> rooms_in) {
  std::vector<matrix::Room *> rooms(rooms_in.begin(), rooms_in.end());
  std::sort(rooms.begin(), rooms.end(),
            [](const matrix::Room *a, const matrix::Room *b) {
              return room_sort_key(*a) < room_sort_key(*b);
            });
  ui->room_list->clear();
  for(auto room : rooms) {
    auto item = new QListWidgetItem;
    item->setText(room->display_name());
    item->setData(Qt::UserRole, QVariant::fromValue(reinterpret_cast<void*>(room)));
    ui->room_list->addItem(item);
  }
}

void MainWindow::set_initial_sync(bool underway) {
  if(underway) {
    progress_->show();
    progress_->set_text(tr("Downloading state..."));
  } else {
    progress_->hide();
  }
}
