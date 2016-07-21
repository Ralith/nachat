#ifndef NATIVE_CHAT_WRAPPING_TEXT_EDIT_HPP_
#define NATIVE_CHAT_WRAPPING_TEXT_EDIT_HPP_

#include <QTextEdit>

class WrappingTextEdit : public QTextEdit {
  Q_OBJECT

public:
  WrappingTextEdit(QWidget *parent = nullptr);

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

signals:
  void send();
  void pageUp();
  void pageDown();

protected:
  void keyPressEvent(QKeyEvent *event) override;

private:
  void document_size_changed(const QSizeF &size);
};

#endif
