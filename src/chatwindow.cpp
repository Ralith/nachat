#include "chatwindow.h"
#include "ui_chatwindow.h"

ChatWindow::ChatWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::ChatWindow) {
  ui->setupUi(this);
}

ChatWindow::~ChatWindow() { delete ui; }

void ChatWindow::add_room(matrix::Room &room) {
  // TODO
}
