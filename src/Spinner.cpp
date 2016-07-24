#include "Spinner.hpp"

#include <chrono>

#include <QPainter>
#include <QConicalGradient>
#include <QDebug>
#include <QObject>

Spinner::Spinner(QWidget *parent) : QWidget(parent) {
  connect(&timer_, SIGNAL(timeout()), this, SLOT(update()));
}

void Spinner::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);

  const qreal rotation_seconds = 2;

  auto t = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now());
  const qreal angle = 360. * static_cast<qreal>(t.time_since_epoch().count() % static_cast<uint64_t>(1000 * rotation_seconds)) / (1000 * rotation_seconds);
  const auto extent = std::min(width(), height());

  painter.translate(extent/2, extent/2);
  painter.rotate(angle);
  painter.drawPixmap(QPoint(-extent/2, -extent/2), pixmap_);
}

void Spinner::showEvent(QShowEvent *) {
  timer_.start(30);
}

void Spinner::hideEvent(QHideEvent *) {
  timer_.stop();
}

QSize Spinner::sizeHint() const {
  auto extent = fontMetrics().height() * 4;
  return QSize(extent, extent);
}

void Spinner::resizeEvent(QResizeEvent *) {
  const auto extent = std::min(width(), height());
  pixmap_ = QPixmap(extent, extent);
  pixmap_.fill(Qt::transparent);
  QPainter painter(&pixmap_);
  painter.setRenderHint(QPainter::Antialiasing);
  paint(palette().color(QPalette::Shadow), palette().color(QPalette::Base), painter, extent);
}

void Spinner::paint(const QColor &head, const QColor &tail, QPainter &painter, int extent) {
  constexpr qreal margin = 1;
  const qreal thickness = extent / 7.0;
  const qreal inset = margin + thickness / 2.0;
  const qreal diameter = extent - thickness - 2*margin;

  const qreal angle = 0;
  const qreal angular_gap = 45;

  QConicalGradient gradient;
  gradient.setCenter(extent/2.0, extent/2.0);
  gradient.setAngle(angle - angular_gap / 2.0);
  gradient.setColorAt(0, head);
  gradient.setColorAt(1, tail);

  QPen pen(QBrush(gradient), thickness);
  pen.setCapStyle(Qt::RoundCap);

  QPainterPath path;
  path.arcMoveTo(inset, inset, diameter, diameter, angle);
  path.arcTo(inset, inset, diameter, diameter, angle, 360 - angular_gap);

  painter.setPen(pen);
  painter.drawPath(path);
}
