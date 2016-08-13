#include "EventSourceView.hpp"
#include "ui_EventSourceView.h"

#include <QJsonDocument>

EventSourceView::EventSourceView(const QJsonObject &obj, QWidget *parent) :
  QWidget(parent),
  ui(new Ui::EventSourceView)
{
  ui->setupUi(this);
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowFlags(Qt::Window);

  ui->text->setPlainText(QString::fromUtf8(QJsonDocument(obj).toJson()));
}

EventSourceView::~EventSourceView()
{
  delete ui;
}
