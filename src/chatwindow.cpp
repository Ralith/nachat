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

void ChatWindow::add_or_focus_view(RoomView &view) {
  auto it = tabs_.find(&view);
  if(it == tabs_.end()) {
    it = tabs_.insert(&view).first;
    auto i = ui->tab_widget->addTab(&view, view.windowTitle());
    ui->tab_widget->setTabText(i, view.room().state().pretty_name());
    connect(&view.room(), &matrix::Room::state_changed, this, &ChatWindow::update_labels);
  }

  ui->tab_widget->setCurrentWidget(*it);
}

void ChatWindow::update_labels() {
  for(int i = 0; i < ui->tab_widget->count(); ++i) {
    auto &view = *reinterpret_cast<RoomView*>(ui->tab_widget->widget(i));
    ui->tab_widget->setTabText(i, view.room().state().pretty_name());
  }
  tab_selected(ui->tab_widget->currentIndex());
}

void ChatWindow::tab_selected(int i) {
  if(i == -1) return;
  auto &view = *static_cast<RoomView*>(ui->tab_widget->widget(i));
  setWindowTitle(view.room().state().pretty_name());
}
