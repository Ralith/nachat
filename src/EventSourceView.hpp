#ifndef EVENTSOURCEVIEW_HPP
#define EVENTSOURCEVIEW_HPP

#include <QWidget>

namespace Ui {
class EventSourceView;
}

class QJsonObject;

class EventSourceView : public QWidget {
  Q_OBJECT

public:
  explicit EventSourceView(const QJsonObject &source, QWidget *parent = nullptr);
  ~EventSourceView();

private:
  Ui::EventSourceView *ui;
};

#endif // EVENTSOURCEVIEW_HPP
