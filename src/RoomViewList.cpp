#include "RoomViewList.hpp"

#include <QMenu>
#include <QContextMenuEvent>
#include <QScrollBar>

#include "matrix/Room.hpp"

RoomViewList::RoomViewList(QWidget *parent) : QListWidget(parent), menu_(new QMenu(this)) {
  connect(this, &QListWidget::currentItemChanged, [this](QListWidgetItem *item, QListWidgetItem *previous) {
      (void)previous;
      if(item != nullptr) {
        auto &room = *reinterpret_cast<matrix::Room *>(item->data(Qt::UserRole).value<quintptr>());
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
  auto pop_out_action = menu_->addAction(QIcon::fromTheme("window-open"), tr("&Pop out"));
  connect(pop_out_action, &QAction::triggered, [this]() {
      pop_out(*context_);
      release(*context_);
    });
  auto close = menu_->addAction(QIcon::fromTheme("window-close"), tr("&Close"));
  connect(close, &QAction::triggered, [this]() {
      release(*context_);
    });

  QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  setSizePolicy(policy);
}

void RoomViewList::add(matrix::Room &room) {
  auto item = new QListWidgetItem;
  item->setToolTip(room.id());
  item->setText(room.pretty_name());
  item->setData(Qt::UserRole, QVariant::fromValue(reinterpret_cast<quintptr>(&room)));
  addItem(item);
  items_.emplace(&room, item);
  claimed(room);
  update();
}

void RoomViewList::release(matrix::Room &room) {
  auto it = items_.find(&room);
  assert(it != items_.end());
  delete it->second;
  items_.erase(it);
  released(room);
}

void RoomViewList::activate(matrix::Room &room) {
  auto &item = *items_.at(&room);
  scrollToItem(&item);
  setCurrentItem(&item);
  activated(room);
}

void RoomViewList::update_name(matrix::Room &room) {
  items_.at(&room)->setText(room.pretty_name());
}

void RoomViewList::contextMenuEvent(QContextMenuEvent *e) {
  if(auto item = itemAt(e->pos())) {
    context_ = reinterpret_cast<matrix::Room *>(item->data(Qt::UserRole).value<quintptr>());
    menu_->popup(e->globalPos());
  }
}

QSize RoomViewList::sizeHint() const {
  auto margins = contentsMargins();
  return QSize(sizeHintForColumn(0) + verticalScrollBar()->sizeHint().width() + margins.left() + margins.right(),
               fontMetrics().lineSpacing() + horizontalScrollBar()->sizeHint().height());
}
