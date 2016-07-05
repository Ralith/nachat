#include "chatwindow.h"
#include "ui_chatwindow.h"

#include <QIcon>

#include "matrix/Room.hpp"

#include "roomview.h"

ChatWindow::ChatWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::ChatWindow) {
  ui->setupUi(this);
  connect(ui->tab_widget, &QTabWidget::currentChanged, this, &ChatWindow::tab_selected);
}

ChatWindow::~ChatWindow() { delete ui; }

void ChatWindow::add_or_focus(matrix::Room &room) {
  auto it = tabs_.find(&room);
  if(it == tabs_.end()) {
    it = tabs_.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(&room),
      std::forward_as_tuple(new RoomView(room))).first;
    auto &view = *it->second;
    ui->tab_widget->addTab(&view, room.pretty_name());
    connect(&room, &matrix::Room::state_changed, this, &ChatWindow::update_labels);
  }

  ui->tab_widget->setCurrentWidget(it->second.get());
}

void ChatWindow::update_labels() {
  for(int i = 0; i < ui->tab_widget->count(); ++i) {
    auto &view = *reinterpret_cast<RoomView*>(ui->tab_widget->widget(i));
    ui->tab_widget->setTabText(i, view.room().pretty_name());
  }
  tab_selected(ui->tab_widget->currentIndex());
}

void ChatWindow::tab_selected(int i) {
  if(i == -1) return;
  auto &view = *static_cast<RoomView*>(ui->tab_widget->widget(i));
  setWindowTitle(view.room().pretty_name());
}
