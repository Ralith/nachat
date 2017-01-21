#include "pixmaps.hpp"

#include <QMimeDatabase>

namespace matrix {

QPixmap decode(const QString &type, const QByteArray &data) {
  QPixmap pixmap;
  pixmap.loadFromData(data, QMimeDatabase().mimeTypeForName(type.toUtf8()).preferredSuffix().toUtf8().constData());
  if(pixmap.isNull()) pixmap.loadFromData(data);
  return pixmap;
}

}
