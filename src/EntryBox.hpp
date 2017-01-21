#ifndef NATIVE_CHAT_ENTRY_BOX_HPP_
#define NATIVE_CHAT_ENTRY_BOX_HPP_

#include <deque>

#include <QTextEdit>

class QCompleter;
class QAbstractListModel;

class EntryBox : public QTextEdit {
  Q_OBJECT

public:
  EntryBox(QAbstractListModel *members, QWidget *parent = nullptr);

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

  void send();

signals:
  void message(const QString &);
  void command(const QString &name, const QString &args);
  void pageUp();
  void pageDown();
  void activity();

protected:
  void keyPressEvent(QKeyEvent *event) override;

private:
  std::deque<QString> true_history_, working_history_;
  size_t history_index_;
  QCompleter *completer_;

  void text_changed();
};

#endif
