#ifndef NATIVE_CHAT_ENTRY_BOX_HPP_
#define NATIVE_CHAT_ENTRY_BOX_HPP_

#include <deque>

#include <QTextEdit>

class EntryBox : public QTextEdit {
  Q_OBJECT

public:
  EntryBox(QWidget *parent = nullptr);

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

signals:
  void send();
  void pageUp();
  void pageDown();

protected:
  void keyPressEvent(QKeyEvent *event) override;

private:
  std::deque<QString> true_history_, working_history_;
  size_t history_index_;

  void text_changed();
};

#endif
