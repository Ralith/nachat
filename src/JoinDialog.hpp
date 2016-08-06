#ifndef NATIVE_CHAT_JOIN_DIALOG_HPP_
#define NATIVE_CHAT_JOIN_DIALOG_HPP_

#include <QDialog>

namespace Ui {
class JoinDialog;
}

class JoinDialog : public QDialog {
  Q_OBJECT

public:
  JoinDialog(QWidget *parent = nullptr);
  ~JoinDialog();

  QString room();

  void accept() override;

private:
  Ui::JoinDialog *ui;
};

#endif
