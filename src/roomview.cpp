#include "roomview.h"
#include "ui_roomview.h"

#include <QAbstractTextDocumentLayout>

#include "matrix/Room.hpp"
#include "matrix/User.hpp"

RoomView::RoomView(matrix::Room &room, QWidget *parent)
    : QWidget(parent), ui(new Ui::RoomView), room_(room) {
  ui->setupUi(this);
  setFocusProxy(ui->entry);

  // Fit to text. Note that QPlainTextEdit returns line count instead of pixel height here for some reason, so we use
  // QTextEdit instead.
  connect(ui->entry->document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
          [this](const QSizeF &size) {
            auto margins = ui->entry->contentsMargins();
            // TODO: Set hint instead of maximum height and replace vertical view layout with splitter
            ui->entry->setMaximumHeight(size.height() + margins.top() + margins.bottom());
          });

  connect(&room, &matrix::Room::users_changed, [this]() {
      update_users();
    });
  update_users();
}

RoomView::~RoomView() { delete ui; }

void RoomView::update_users() {
  auto users = room_.users();
  std::sort(users.begin(), users.end(),
            [](const matrix::User *a, const matrix::User *b) {
              // TODO: Sort by power level first?
              return a->pretty_name() < b->pretty_name();
            });
  ui->userlist->clear();
  for(auto user : users) {
    auto item = new QListWidgetItem;
    item->setText(user->pretty_name());
    item->setToolTip(user->id());
    item->setData(Qt::UserRole, QVariant::fromValue(const_cast<void*>(reinterpret_cast<const void*>(user))));
    ui->userlist->addItem(item);
  }
  auto scrollbar_width = ui->userlist->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, ui->userlist);
  auto margins = ui->userlist->contentsMargins();
  ui->userlist->setMaximumWidth(ui->userlist->sizeHintForColumn(0) + scrollbar_width + margins.left() + margins.right());
}

void RoomView::fit_text() {
  QFontMetrics metrics(ui->entry->font());
  auto t = ui->entry->toPlainText();
  int height = t.size() ? metrics.boundingRect(t).height() : metrics.height();
  ui->entry->setMinimumHeight(height);
  ui->entry->setMaximumHeight(height);
}
