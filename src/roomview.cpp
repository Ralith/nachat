#include "roomview.h"
#include "ui_roomview.h"

#include <QAbstractTextDocumentLayout>

#include "matrix/Room.hpp"
#include "matrix/Member.hpp"

namespace {

QString member_sort_key(const matrix::Room &r, const matrix::Member &m) {
  const auto &n = r.member_name(m);
  int i = 0;
  while((n[i] == '@') && (i < n.size())) {
    ++i;
  }
  if(i == n.size()) return n.toCaseFolded();
  return QString(n.data() + i, n.size() - i).toCaseFolded();
}

}

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

  connect(&room, &matrix::Room::members_changed, this, &RoomView::update_members);
  connect(&room, &matrix::Room::member_names_changed, this, &RoomView::update_members);
  update_members();
}

RoomView::~RoomView() { delete ui; }

void RoomView::update_members() {
  auto members = room_.members();
  std::sort(members.begin(), members.end(),
            [this](const matrix::Member *a, const matrix::Member *b) {
              return member_sort_key(room_, *a) < member_sort_key(room_, *b);
            });
  ui->memberlist->clear();
  for(auto member : members) {
    auto item = new QListWidgetItem;
    item->setText(room_.member_name(*member));
    item->setToolTip(member->id());
    item->setData(Qt::UserRole, QVariant::fromValue(const_cast<void*>(reinterpret_cast<const void*>(member))));
    ui->memberlist->addItem(item);
  }
  auto scrollbar_width = ui->memberlist->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, ui->memberlist);
  auto margins = ui->memberlist->contentsMargins();
  ui->memberlist->setMaximumWidth(ui->memberlist->sizeHintForColumn(0) + scrollbar_width + margins.left() + margins.right());
}

void RoomView::fit_text() {
  QFontMetrics metrics(ui->entry->font());
  auto t = ui->entry->toPlainText();
  int height = t.size() ? metrics.boundingRect(t).height() : metrics.height();
  ui->entry->setMinimumHeight(height);
  ui->entry->setMaximumHeight(height);
}
