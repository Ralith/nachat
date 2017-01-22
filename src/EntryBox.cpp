#include "EntryBox.hpp"

#include <QDebug>
#include <QAbstractTextDocumentLayout>
#include <QGuiApplication>
#include <QCompleter>
#include <QAbstractItemView>
#include <QScrollBar>

static constexpr size_t INPUT_HISTORY_SIZE = 127;

EntryBox::EntryBox(QAbstractListModel *members, QWidget *parent)
  : QTextEdit(parent), true_history_(INPUT_HISTORY_SIZE), working_history_(1), history_index_(0), completer_{new QCompleter{members, this}} {
  connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this, &EntryBox::updateGeometry);
  QSizePolicy policy(QSizePolicy::Ignored, QSizePolicy::Maximum);
  policy.setHorizontalStretch(1);
  policy.setVerticalStretch(1);
  setSizePolicy(policy);
  setAcceptRichText(false);
  document()->setDocumentMargin(2);
  working_history_.push_back("");
  connect(this, &QTextEdit::textChanged, this, &EntryBox::text_changed);

  completer_->setWidget(this);
  completer_->setCaseSensitivity(Qt::CaseInsensitive);
  completer_->setCompletionMode(QCompleter::PopupCompletion);
  connect(completer_, static_cast<void(QCompleter::*)(const QString &)>(&QCompleter::activated), [this](const QString& completion) {
      QTextCursor tc = textCursor();
      tc.movePosition(QTextCursor::StartOfWord, QTextCursor::KeepAnchor);
      tc.insertText(completion);
      setTextCursor(tc);
      after_completion(completion.size());
    });
}

QSize EntryBox::sizeHint() const {
  ensurePolished();
  auto margins = viewportMargins();
  margins += document()->documentMargin();
  QSize size = document()->size().toSize();
  size.rwidth() += margins.left() + margins.right();
  size.rheight() += margins.top() + margins.bottom();
  return size;
}

QSize EntryBox::minimumSizeHint() const {
  ensurePolished();
  auto margins = viewportMargins();
  margins += document()->documentMargin();
  margins += contentsMargins();
  QSize size(fontMetrics().averageCharWidth() * 10, fontMetrics().lineSpacing() + margins.top() + margins.bottom());
  return size;
}

void EntryBox::keyPressEvent(QKeyEvent *event) {
  activity();

  if(completer_->popup()->isVisible()) {
    switch (event->key()) {
    case Qt::Key_Enter:
    case Qt::Key_Return:
    case Qt::Key_Escape:
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
      event->ignore();
      return; // let the completer do default behavior
    default:
      completer_->popup()->hide();
      break;
    }
  }

  auto modifiers = QGuiApplication::keyboardModifiers();
  switch(event->key()) {
  case Qt::Key_Return:
  case Qt::Key_Enter:
    if(!(modifiers & Qt::ShiftModifier)) {
      send();
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
  case Qt::Key_Tab: {
    QTextCursor tc = textCursor();
    tc.movePosition(QTextCursor::StartOfWord, QTextCursor::KeepAnchor);
    const QString word = tc.selectedText();
    if(word != completer_->completionPrefix()) {
      completer_->setCompletionPrefix(word);
    }
    if(completer_->completionCount() == 1) {
      QString completion = completer_->currentCompletion();
      tc.insertText(completion);
      after_completion(completion.size());
    } else {
      QRect cr = cursorRect();
      cr.setWidth(completer_->popup()->sizeHintForColumn(0)
                  + completer_->popup()->verticalScrollBar()->sizeHint().width());
      completer_->complete(cr);
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
  if(true_history_.size() == INPUT_HISTORY_SIZE) true_history_.pop_back();
  true_history_.push_front(toPlainText());
  working_history_ = true_history_;
  working_history_.push_front("");
  history_index_ = 0;

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

  clear();
}

void EntryBox::after_completion(int completion_size) {
  QTextCursor tc = textCursor();
  if(tc.position() == completion_size) {
    // Completion is the first thing in the message
    tc.insertText(": ");
  }
}
