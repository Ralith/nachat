#include "RoomMenu.hpp"

#include <QFileDialog>
#include <QPointer>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>

#include "matrix/Session.hpp"
#include "MessageBox.hpp"

RoomMenu::RoomMenu(matrix::Room &room, QWidget *parent) : QMenu(parent), room_(room) {
  auto file_dialog = new QFileDialog(parent);
  auto upload = addAction(QIcon::fromTheme("document-open"), tr("Upload &file..."));
  connect(upload, &QAction::triggered, file_dialog, &QDialog::open);
  connect(file_dialog, &QFileDialog::fileSelected, this, &RoomMenu::upload_file);
  addSeparator();
  auto leave = addAction(QIcon::fromTheme("system-log-out"), tr("Leave"));
  connect(leave, &QAction::triggered, &room_, &matrix::Room::leave);
}

void RoomMenu::upload_file(const QString &path) {
  auto file = std::make_shared<QFile>(path);
  QFileInfo info(*file);
  if(!file->open(QIODevice::ReadOnly)) {
    MessageBox::critical(tr("Error opening file"), tr("Couldn't open %1: %2").arg(info.fileName()).arg(file->errorString()), parentWidget());
    return;
  }

  const QString &type = QMimeDatabase().mimeTypeForFile(info).name();
  auto reply = room_.session().upload(*file, type, info.fileName());
  QPointer<matrix::Room> room(&room_);
  // This closure captures 'file' to ensure its outlives the network request
  connect(reply, &matrix::ContentPost::success, [file, room, info, type](const QString &uri) {
      if(!room) return;
      room->send_file(uri, info.fileName(), type, info.size());
    });
  QPointer<QWidget> parent(parentWidget());
  connect(reply, &matrix::ContentPost::error, [parent, info](const QString &msg) {
      MessageBox::critical(tr("Error uploading file"), tr("Couldn't upload %1: %2").arg(info.fileName()).arg(msg), parent);
    });
}
