#include "ChatWindow.hpp"
#include "ui_ChatWindow.h"

#include <QIcon>
#include <QCloseEvent>

#include "matrix/Room.hpp"

#include "roomview.h"

ChatWindow::ChatWindow(QWidget *parent)
    : QWidget(parent), ui(new Ui::ChatWindow) {
  ui->setupUi(this);
  setAttribute(Qt::WA_DeleteOnClose);
  connect(ui->room_stack, &QStackedWidget::currentChanged, this, &ChatWindow::current_changed);
  ui->splitter->setCollapsible(1, false);
  ui->splitter->setSizes({0, -1});

  connect(ui->room_list, &QListWidget::currentItemChanged, [this](QListWidgetItem *item, QListWidgetItem *previous) {
      (void)previous;
      auto &room = *reinterpret_cast<matrix::Room *>(item->data(Qt::UserRole).value<quintptr>());
      ui->room_stack->setCurrentWidget(rooms_.at(&room).view);
    });
}

ChatWindow::~ChatWindow() { delete ui; }

void ChatWindow::add_or_focus(matrix::Room &room) {
  QListWidgetItem *item;
  int index;
  auto it = rooms_.find(&room);
  if(it == rooms_.end()) {
    item = new QListWidgetItem;
    item->setToolTip(room.id());
    item->setText(room.pretty_name());
    item->setData(Qt::UserRole, QVariant::fromValue(reinterpret_cast<quintptr>(&room)));
    ui->room_list->addItem(item);
    if(ui->room_list->count() == 2) {
      ui->splitter->setSizes({ui->room_list->sizeHintForColumn(0), -1});
    }

    it = rooms_.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(&room),
      std::forward_as_tuple(RoomInfo{new RoomView(room, this), item})).first;
    auto &view = *it->second.view;
    index = ui->room_stack->addWidget(&view);
    connect(&room, &matrix::Room::state_changed, this, &ChatWindow::update_title);
    connect(&room, &matrix::Room::state_changed, this, &ChatWindow::update_room_list);
  } else {
    item = it->second.item;
    index = ui->room_stack->indexOf(it->second.view);
  }

  ui->room_list->scrollToItem(item);
  ui->room_list->setCurrentItem(item);
  ui->room_stack->setCurrentIndex(index);
}

void ChatWindow::update_title() {
  if(auto w = ui->room_stack->currentWidget()) {
    setWindowTitle(static_cast<RoomView*>(w)->room().pretty_name());
  } else {
    setWindowTitle("");
  }
}

void ChatWindow::update_room_list() {
  for(int i = 0; i < ui->room_list->count(); ++i) {
    auto &item = *ui->room_list->item(i);
    auto &room = *reinterpret_cast<matrix::Room *>(item.data(Qt::UserRole).value<quintptr>());
    item.setText(room.pretty_name());
  }
}

void ChatWindow::current_changed(int i) {
  (void)i;
  update_title();
}

void ChatWindow::focusInEvent(QFocusEvent *) {
  focused();
}

void ChatWindow::closeEvent(QCloseEvent *evt) {
  for(auto &r : rooms_) {
    released(r.first);
  }
  evt->accept();
}
