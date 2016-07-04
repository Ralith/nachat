#ifndef NATIVE_CHAT_QSTRING_HASH_HPP_
#define NATIVE_CHAT_QSTRING_HASH_HPP_

#include <QHash>

struct QStringHash {
  size_t operator()(const QString &s) const { return qHash(s); }
};

#endif
