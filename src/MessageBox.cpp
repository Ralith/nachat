#include "MessageBox.hpp"

void MessageBox::critical(const QString &title, const QString &message, QWidget *parent) {
  auto box = new QMessageBox(QMessageBox::Critical, title, message, QMessageBox::Close, parent);
  box->setAttribute(Qt::WA_DeleteOnClose);
  box->open();
}
