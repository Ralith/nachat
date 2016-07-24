#ifndef NATIVE_CHAT_SPINNER_HPP_
#define NATIVE_CHAT_SPINNER_HPP_

#include <QWidget>
#include <QTimer>
#include <QPixmap>

class Spinner : public QWidget {
  Q_OBJECT
public:
  Spinner(QWidget *parent = nullptr);

  QSize sizeHint() const override;

  static void paint(const QColor &head, const QColor &tail, QPainter &, int extent);

protected:
  void paintEvent(QPaintEvent *) override;
  void showEvent(QShowEvent *) override;
  void hideEvent(QHideEvent *) override;
  void resizeEvent(QResizeEvent *) override;

private:
  QTimer timer_;
  QPixmap pixmap_;
};

#endif
