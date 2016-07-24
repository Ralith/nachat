#include "RedactDialog.hpp"
#include "ui_RedactDialog.h"

RedactDialog::RedactDialog(QWidget *parent) : QDialog(parent), ui_(new Ui::RedactDialog) {
  ui_->setupUi(this);
}
RedactDialog::~RedactDialog() { delete ui_; }

QString RedactDialog::reason() const {
  return ui_->reason->toPlainText();
}
