#ifndef NATIVE_CHAT_REDACT_DIALOG_HPP_
#define NATIVE_CHAT_REDACT_DIALOG_HPP_

#include <QDialog>

namespace Ui {
class RedactDialog;
}

class RedactDialog : public QDialog {
  Q_OBJECT

public:
  explicit RedactDialog(QWidget *parent = 0);
  ~RedactDialog();

  QString reason() const;

private:
  Ui::RedactDialog *ui_;
};

#endif
