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

void ChatWindow::add_or_focus_room(matrix::Room &room) {
  auto it = tabs_.find(&room);
  if(it == tabs_.end()) {
    it = tabs_.insert(std::make_pair(&room, new RoomView(room, this))).first;
    auto &view = *it->second;
    ui->tab_widget->addTab(&view, room.current_state().pretty_name());

    connect(&room, &matrix::Room::state_changed, [this, &view, &room]() {
        update_label(room, view);
      });
    connect(&room, &matrix::Room::notification_count_changed, [this, &view, &room]() {
        auto index = ui->tab_widget->indexOf(&view);
        if(room.notification_count() == 0) {
          ui->tab_widget->setTabIcon(index, QIcon());
        } else {
          ui->tab_widget->setTabIcon(index, QIcon::fromTheme("mail-unread"));
        }
      });
    connect(&room, &matrix::Room::highlight_count_changed, [this, &view, &room]() {
        update_label(room, view);
      });
  }

  ui->tab_widget->setCurrentWidget(it->second);
}

void ChatWindow::update_label(matrix::Room &room, RoomView &view) {
  QString label;
  if(room.highlight_count() != 0) {
    label = tr("%1 (%2)").arg(room.current_state().pretty_name()).arg(room.highlight_count());
  } else {
    label = room.current_state().pretty_name();
  }
  ui->tab_widget->setTabText(ui->tab_widget->indexOf(&view), std::move(label));
}

void ChatWindow::tab_selected(int i) {
  auto &view = *static_cast<RoomView*>(ui->tab_widget->widget(i));
  setWindowTitle(view.room().current_state().pretty_name());
}
