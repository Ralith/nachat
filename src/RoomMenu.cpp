#include "RoomMenu.hpp"

#include <QFileDialog>

RoomMenu::RoomMenu(matrix::Room &room, QWidget *parent) : QMenu(parent), room_(room) {
  auto file_dialog = new QFileDialog(this);
  auto upload = addAction(QIcon::fromTheme("document-open"), tr("Upload &file..."));
  connect(upload, &QAction::triggered, file_dialog, &QDialog::open);
  connect(file_dialog, &QFileDialog::fileSelected, &room, &matrix::Room::send_file);
  addSeparator();
  auto leave = addAction(QIcon::fromTheme("system-log-out"), tr("Leave"));
  connect(leave, &QAction::triggered, &room_, &matrix::Room::leave);
}
