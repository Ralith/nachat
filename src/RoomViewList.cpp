#include "RoomViewList.hpp"

#include <QMenu>
#include <QContextMenuEvent>
#include <QScrollBar>
#include <qstringbuilder.h>
#include <QDebug>

#include "matrix/Room.hpp"

RoomViewList::RoomViewList(QWidget *parent) : QListWidget(parent), menu_(new QMenu(this)) {
  connect(this, &QListWidget::currentItemChanged, [this](QListWidgetItem *item, QListWidgetItem *previous) {
      (void)previous;
      if(item != nullptr) {
        auto room = item->data(Qt::UserRole).toString();
        activated(room);
      }
    });

  auto move_up = menu_->addAction(tr("Move up"));
  connect(move_up, &QAction::triggered, [this]() {
      auto r = row(items_.at(context_));
      auto item = takeItem(r);
      insertItem(r > 0 ? r - 1 : 0, item);
      setCurrentItem(item);
    });
  auto move_down = menu_->addAction(tr("Move down"));
  connect(move_down, &QAction::triggered, [this]() {
      auto r = row(items_.at(context_));
      auto item = takeItem(r);
      insertItem(r+1, item);
      setCurrentItem(item);
    });
  menu_->addSeparator();
  auto pop_out_action = menu_->addAction(QIcon::fromTheme("window-open"), tr("&Pop out"));
  connect(pop_out_action, &QAction::triggered, [this]() {
      pop_out(context_);
    });
  auto close = menu_->addAction(QIcon::fromTheme("window-close"), tr("&Close"));
  connect(close, &QAction::triggered, [this]() {
      release(context_);
    });

  QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  setSizePolicy(policy);
}

void RoomViewList::add(matrix::Room &room) {
  auto item = new QListWidgetItem;
  item->setToolTip(room.id());
  item->setData(Qt::UserRole, room.id());
  addItem(item);
  auto r = items_.emplace(room.id(), item);
  assert(r.second);
  claimed(room.id());
  update_display(room);
  updateGeometry();
  viewport()->update();
}

void RoomViewList::release(const matrix::RoomID &room) {
  auto it = items_.find(room);
  assert(it != items_.end());
  delete it->second;
  items_.erase(it);
  released(room);
  updateGeometry();
  viewport()->update();
}

void RoomViewList::activate(const matrix::RoomID &room) {
  auto &item = *items_.at(room);
  scrollToItem(&item);
  setCurrentItem(&item);
  activated(room);
}

void RoomViewList::update_display(matrix::Room &room) {
  auto &i = *items_.at(room.id());
  i.setText(room.pretty_name_highlights());
  auto font = i.font();
  font.setBold(room.highlight_count() != 0 || room.notification_count() != 0);
  i.setFont(font);
}

void RoomViewList::contextMenuEvent(QContextMenuEvent *e) {
  if(auto item = itemAt(e->pos())) {
    context_ = item->data(Qt::UserRole).toString();
    menu_->popup(e->globalPos());
  }
}

QSize RoomViewList::sizeHint() const {
  auto margins = viewportMargins();
  return QSize(sizeHintForColumn(0) + verticalScrollBar()->sizeHint().width() + margins.left() + margins.right(),
               fontMetrics().lineSpacing() + horizontalScrollBar()->sizeHint().height());
}
