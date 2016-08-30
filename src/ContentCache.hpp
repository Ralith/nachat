#ifndef NACHAT_CONTENT_CACHE_HPP_
#define NACHAT_CONTENT_CACHE_HPP_

#include <unordered_map>
#include <experimental/optional>

#include <QObject>
#include <QPixmap>

#include "matrix/Content.hpp"

class ThumbnailCache : public QObject {
  Q_OBJECT

public:
  void ref(const matrix::Thumbnail &);
  void unref(const matrix::Thumbnail &);

  const std::experimental::optional<QPixmap> &get(const matrix::Thumbnail &) const;
  void set(const matrix::Thumbnail &, QPixmap);
  
signals:
  void needs(const matrix::Thumbnail &);
  void updated();

private:
  struct Item {
    size_t refs = 0;
    std::experimental::optional<QPixmap> pixmap;
  };

  std::unordered_map<matrix::Thumbnail, Item> items_;
};

class ThumbnailRef {
public:
  ThumbnailRef(const matrix::Thumbnail &content, ThumbnailCache &cache) : content_{content}, cache_{&cache} {
    cache_->ref(content);
  }
  ~ThumbnailRef() {
    if(cache_) cache_->unref(content_);
  }

  ThumbnailRef(const ThumbnailRef &other) : content_{other.content_}, cache_{other.cache_} {
    cache_->ref(content_);
  }
  ThumbnailRef(ThumbnailRef &&other) : content_{other.content_}, cache_{other.cache_} {
    other.cache_ = nullptr;
  }

  ThumbnailRef &operator=(ThumbnailRef &&other) {
    if(cache_) cache_->unref(content_);
    content_ = other.content_;
    cache_ = other.cache_;
    other.cache_ = nullptr;
    return *this;
  }
  ThumbnailRef &operator=(const ThumbnailRef &other) {
    content_ = other.content_;
    cache_ = other.cache_;
    cache_->ref(content_);
    return *this;
  }

  const matrix::Thumbnail &content() { return content_; }

  const std::experimental::optional<QPixmap> &operator*() const {
    return cache_->get(content_);
  }

private:
  matrix::Thumbnail content_;

  ThumbnailCache *cache_;
};

#endif
