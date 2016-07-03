#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <memory>

#include <QDialog>
#include <QSettings>

#include "matrix/matrix.hpp"

namespace Ui {
class LoginDialog;
}

class LoginDialog : public QDialog {
  Q_OBJECT

public:
  LoginDialog(matrix::Matrix &, QWidget *parent = 0);
  ~LoginDialog();

  void accept() override;

  std::unique_ptr<matrix::Session> session() { return std::move(session_); }

private:
  Ui::LoginDialog *ui;
  QSettings settings_;
  matrix::Matrix &m_;

  std::unique_ptr<matrix::Session> session_;
};

#endif // LOGINDIALOG_H
