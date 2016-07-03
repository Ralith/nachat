#ifndef ROOMVIEW_H
#define ROOMVIEW_H

#include <QWidget>
#include <QFontMetrics>

namespace Ui {
class RoomView;
}

class RoomView : public QWidget
{
  Q_OBJECT

public:
  explicit RoomView(QWidget *parent = 0);
  ~RoomView();

private:
  Ui::RoomView *ui;
  QFontMetrics metrics_;

  void fit_text();
};

#endif // ROOMVIEW_H
