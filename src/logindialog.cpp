#include "logindialog.h"
#include "ui_logindialog.h"

#include <QMessageBox>

LoginDialog::LoginDialog(matrix::Matrix &m, QWidget *parent)
    : QDialog(parent), ui(new Ui::LoginDialog), m_(m) {
  ui->setupUi(this);

  ui->buttonBox->addButton(tr("Sign In"), QDialogButtonBox::AcceptRole);
  ui->buttonBox->addButton(tr("Quit"), QDialogButtonBox::RejectRole);

  auto username = settings_.value("login/username");
  if(!username.isNull()) {
    ui->username->setText(settings_.value("login/username").toString());
    ui->password->setFocus(Qt::OtherFocusReason);
  }

  auto homeserver = settings_.value("login/homeserver");
  if(!homeserver.isNull()) {
    ui->homeserver->setText(homeserver.toString());
  }

  connect(&m_, &matrix::Matrix::login_error, [&](QString err){
      setDisabled(false);
      QMessageBox msg(QMessageBox::Critical, tr("Login Error"), err, QMessageBox::Ok, this);
      msg.exec();
    });
  connect(&m_, &matrix::Matrix::logged_in, [this](matrix::Session *session){
      session_ = std::unique_ptr<matrix::Session>(session);
      settings_.setValue("login/username", ui->username->text());
      settings_.setValue("login/homeserver", ui->homeserver->text());
      settings_.setValue("session/access_token", session->access_token());
      settings_.setValue("session/user_id", session->user_id());
      QDialog::accept();
    });
}

LoginDialog::~LoginDialog() { delete ui; }

void LoginDialog::accept() {
  setDisabled(true);
  m_.login(ui->homeserver->text(), ui->username->text(), ui->password->text());
}
