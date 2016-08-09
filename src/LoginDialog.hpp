#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <memory>

#include <QDialog>

#include "matrix/Matrix.hpp"

namespace Ui {
class LoginDialog;
}

class LoginDialog : public QDialog {
  Q_OBJECT

public:
  LoginDialog(QWidget *parent = nullptr);
  ~LoginDialog();

  void accept() override;

  QString username() const;
  QString password() const;
  QString homeserver() const;

private:
  Ui::LoginDialog *ui;
};

#endif // LOGINDIALOG_H
