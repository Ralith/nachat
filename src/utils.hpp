#ifndef NACHAT_UTILS_HPP_
#define NACHAT_UTILS_HPP_

inline static QSize initial_icon_size(QWidget &widget) {
  int i = widget.style()->pixelMetric(QStyle::PM_ListViewIconSize, nullptr, &widget);
  return QSize(i, i);
}

#endif
