#include "JoinDialog.hpp"
#include "ui_JoinDialog.h"

JoinDialog::JoinDialog(QWidget *parent) : QDialog(parent), ui(new Ui::JoinDialog) {
  ui->setupUi(this);
}

JoinDialog::~JoinDialog() { delete ui; }

QString JoinDialog::room() { return ui->lineEdit->text(); }

void JoinDialog::accept() {
  setEnabled(false);
  setResult(QDialog::Accepted);
  accepted();
}
