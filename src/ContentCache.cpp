#include "ContentCache.hpp"

using matrix::Thumbnail;

void ThumbnailCache::ref(const Thumbnail &x) {
  auto i = items_.emplace(std::piecewise_construct,
                          std::forward_as_tuple(x),
                          std::forward_as_tuple());
  if(i.second) {
    needs(x);
  } else {
    ++i.first->second.refs;
  }
}

void ThumbnailCache::unref(const Thumbnail &x) {
  auto it = items_.find(x);
  if(it->second.refs == 0) {
    items_.erase(it);
  } else {
    --it->second.refs;
  }
}

const std::experimental::optional<QPixmap> &ThumbnailCache::get(const Thumbnail &x) const {
  return items_.at(x).pixmap;
}

void ThumbnailCache::set(const Thumbnail &x, QPixmap p) {
  auto it = items_.find(x);
  if(it != items_.end()) {
    p.setDevicePixelRatio(device_pixel_ratio_);
    it->second.pixmap = p;
    updated();
  }
}
