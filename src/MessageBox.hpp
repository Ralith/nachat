#ifndef NATIVE_CHAT_MESSAGEBOX_HPP_
#define NATIVE_CHAT_MESSAGEBOX_HPP_

#include <QMessageBox>

class MessageBox {
public:
  static void critical(const QString &title, const QString &message, QWidget *parent = nullptr);
};

#endif
