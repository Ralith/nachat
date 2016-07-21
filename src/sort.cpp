#include "sort.hpp"

QString room_sort_key(const QString &n) {
  int i = 0;
  while((n[i] == '#' || n[i] == '@') && (i < n.size())) {
    ++i;
  }
  if(i == n.size()) return n.toCaseFolded();
  return QString(n.data() + i, n.size() - i).toCaseFolded();
}
