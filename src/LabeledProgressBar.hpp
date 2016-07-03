#ifndef LABELEDPROGRESSBAR_HPP
#define LABELEDPROGRESSBAR_HPP

#include <QWidget>

class QLabel;

namespace Ui {
class LabeledProgressBar;
}

class LabeledProgressBar : public QWidget
{
  Q_OBJECT

public:
  explicit LabeledProgressBar(QWidget *parent = 0);
  ~LabeledProgressBar();

  void set_text(const QString &text);
  void set_minimum(int);
  void set_maximum(int);
  void set_value(int);

private:
  Ui::LabeledProgressBar *ui;
  QLabel *label_;
};

#endif // LABELEDPROGRESSBAR_HPP
