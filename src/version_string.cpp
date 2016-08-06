#include "version.hpp"

#include <QObject>
#include <QString>

namespace version {
QString string() {
  QString result = tag;
  if(commits_since_tag != 0) {
    result += "-" + QString::number(commits_since_tag);
  }
  if(dirty) {
    result += "-" + QObject::tr("dirty");
  }
  return result;
}
}
