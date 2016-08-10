#include "LoginDialog.hpp"
#include "ui_LoginDialog.h"

#include <QSettings>

#include "matrix/Session.hpp"

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::LoginDialog) {
  ui->setupUi(this);

  ui->buttonBox->addButton(tr("Quit"), QDialogButtonBox::RejectRole);
  ui->buttonBox->addButton(tr("Sign In"), QDialogButtonBox::AcceptRole);

  QSettings settings;

  auto username = settings.value("login/username");
  if(!username.isNull()) {
    ui->username->setText(settings.value("login/username").toString());
    ui->password->setFocus(Qt::OtherFocusReason);
  }

  auto homeserver = settings.value("login/homeserver");
  if(!homeserver.isNull()) {
    ui->homeserver->setText(homeserver.toString());
  }  
}

LoginDialog::~LoginDialog() { delete ui; }

void LoginDialog::accept() {
  setDisabled(true);
  setResult(QDialog::Accepted);
  accepted();
}

QString LoginDialog::username() const { return ui->username->text(); }
QString LoginDialog::password() const { return ui->password->text(); }
QString LoginDialog::homeserver() const { return ui->homeserver->text(); }
