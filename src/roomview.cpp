#include "roomview.h"
#include "ui_roomview.h"

RoomView::RoomView(QWidget *parent) : QWidget(parent), ui(new Ui::RoomView), metrics_(ui->entry->font()) {
  ui->setupUi(this);
  connect(ui->entry, &QTextEdit::textChanged, this, &RoomView::fit_text);
}

RoomView::~RoomView() { delete ui; }

void RoomView::fit_text() {
  auto t = ui->entry->toPlainText();
  int height = t.size() ? metrics_.boundingRect(t).height() : metrics_.height();
  ui->entry->setMinimumHeight(height);
  ui->entry->setMaximumHeight(height);
}
