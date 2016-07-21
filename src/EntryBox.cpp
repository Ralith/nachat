#include "EntryBox.hpp"

#include <QDebug>
#include <QAbstractTextDocumentLayout>
#include <QGuiApplication>

EntryBox::EntryBox(QWidget *parent) : QTextEdit(parent) {
  connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
          this, &EntryBox::document_size_changed);
  QSizePolicy policy(QSizePolicy::Ignored, QSizePolicy::Expanding);
  policy.setHorizontalStretch(1);
  policy.setVerticalStretch(1);
  setSizePolicy(policy);
  setAcceptRichText(false);
}

QSize EntryBox::sizeHint() const {
  auto margins = contentsMargins();
  QSize size = document()->size().toSize();
  size.rwidth() += margins.left() + margins.right();
  size.rheight() += margins.top() + margins.bottom();

  return size;
}

QSize EntryBox::minimumSizeHint() const {
  auto margins = contentsMargins();
  return QSize(fontMetrics().averageCharWidth(), fontMetrics().lineSpacing() + margins.top() + margins.bottom());
}

void EntryBox::document_size_changed(const QSizeF &size) {
  auto margins = contentsMargins();
  // FIXME: Should be able to rely on sizeHint and QSizePolicy::Preferred
  setMaximumHeight(size.height() + margins.top() + margins.bottom());
  updateGeometry();
}

void EntryBox::keyPressEvent(QKeyEvent *event) {
  auto modifiers = QGuiApplication::keyboardModifiers();
  // TODO: Autocomplete
  switch(event->key()) {
  case Qt::Key_Return:
  case Qt::Key_Enter:
    if(!(modifiers & Qt::ShiftModifier)) {
      send();
      clear();
      return;
    }
    break;
  case Qt::Key_PageUp:
    pageUp();
    return;
  case Qt::Key_PageDown:
    pageDown();
    return;
  default:
    break;
  }
  QTextEdit::keyPressEvent(event);
}
