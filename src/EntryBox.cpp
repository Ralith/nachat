#include "EntryBox.hpp"

#include <QDebug>
#include <QAbstractTextDocumentLayout>
#include <QGuiApplication>

static constexpr size_t INPUT_HISTORY_SIZE = 127;

EntryBox::EntryBox(QWidget *parent) : QTextEdit(parent), true_history_(INPUT_HISTORY_SIZE), working_history_(1), history_index_(0) {
  connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this, &EntryBox::updateGeometry);
  QSizePolicy policy(QSizePolicy::Ignored, QSizePolicy::Maximum);
  policy.setHorizontalStretch(1);
  policy.setVerticalStretch(1);
  setSizePolicy(policy);
  setAcceptRichText(false);
  document()->setDocumentMargin(2);
  working_history_.push_back("");
  connect(this, &QTextEdit::textChanged, this, &EntryBox::text_changed);
}

QSize EntryBox::sizeHint() const {
  auto margins = viewportMargins();
  margins += document()->documentMargin();
  QSize size = document()->size().toSize();
  size.rwidth() += margins.left() + margins.right();
  size.rheight() += margins.top() + margins.bottom();

  return size;
}

QSize EntryBox::minimumSizeHint() const {
  auto margins = viewportMargins();
  margins += document()->documentMargin();
  return QSize(fontMetrics().averageCharWidth() * 10, fontMetrics().lineSpacing() + margins.top() + margins.bottom());
}

void EntryBox::keyPressEvent(QKeyEvent *event) {
  auto modifiers = QGuiApplication::keyboardModifiers();
  // TODO: Autocomplete
  switch(event->key()) {
  case Qt::Key_Return:
  case Qt::Key_Enter:
    if(!(modifiers & Qt::ShiftModifier)) {
      if(true_history_.size() == INPUT_HISTORY_SIZE) true_history_.pop_back();
      true_history_.push_front(toPlainText());
      working_history_ = true_history_;
      working_history_.push_front("");
      history_index_ = 0;
      send();
      clear();
    } else {
      QTextEdit::keyPressEvent(event);
    }
    break;
  case Qt::Key_PageUp:
    pageUp();
    break;
  case Qt::Key_PageDown:
    pageDown();
    break;
  case Qt::Key_Up: {
    auto initial_cursor = textCursor();
    QTextEdit::keyPressEvent(event);
    if(textCursor() == initial_cursor && history_index_ + 1 < working_history_.size()) {
      ++history_index_;
      setPlainText(working_history_[history_index_]);
      moveCursor(QTextCursor::End);
    }
    break;
  }
  case Qt::Key_Down: {
    auto initial_cursor = textCursor();
    QTextEdit::keyPressEvent(event);
    if(textCursor() == initial_cursor && history_index_ > 0) {
      --history_index_;
      setPlainText(working_history_[history_index_]);
      moveCursor(QTextCursor::End);
    }
    break;
  }
  default:
    QTextEdit::keyPressEvent(event);
    break;
  }
}

void EntryBox::text_changed() {
  working_history_[history_index_] = toPlainText();
}

void EntryBox::send() {
  const QString text = toPlainText();
  if(text.startsWith('/')) {
    int command_end = text.indexOf(' ');
    if(command_end == -1) command_end = text.size();
    const auto &name = text.mid(1, command_end - 1);
    const auto &args = text.mid(command_end + 1);
    if(name.isEmpty()) {
      message(args);
    } else {
      command(name, args);
    }
  } else {
    message(text);
  }
}
