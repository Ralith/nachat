#ifndef NATIVE_CHAT_MATRIX_CONTENT_HPP_
#define NATIVE_CHAT_MATRIX_CONTENT_HPP_

#include <functional>
#include <stdexcept>

#include <QString>
#include <QHash>
#include <QSize>

#include "hash_combine.hpp"

class QUrl;

namespace matrix {

class illegal_content_scheme : public std::invalid_argument {
public:
  illegal_content_scheme() : std::invalid_argument{"content URLs had scheme other than \"mxc\""} {}
};

class Content {
public:
  Content(const QString &host, const QString &id) noexcept : host_(host), id_(id) {}
  explicit Content(const QUrl &url);

  const QString &host() const noexcept { return host_; }
  const QString &id() const noexcept { return id_; }

  QUrl url() const noexcept;
  QUrl url_on(const QUrl &homeserver) const noexcept;

private:
  QString host_, id_;
};

inline bool operator==(const Content &a, const Content &b) {
  return a.host() == b.host() && a.id() == b.id();
}

enum class ThumbnailMethod { CROP, SCALE };

class Thumbnail {
public:
  Thumbnail(Content content, QSize size, ThumbnailMethod method) noexcept : content_(content), size_(size), method_(method) {}

  const Content &content() const noexcept { return content_; }
  const QSize &size() const noexcept { return size_; }
  ThumbnailMethod method() const noexcept { return method_; }

  QUrl url_on(const QUrl &homeserver) const;

private:
  Content content_;
  QSize size_;
  ThumbnailMethod method_;
};

inline bool operator==(const Thumbnail &a, const Thumbnail &b) {
  return a.content() == b.content() && a.size() == b.size() && a.method() == b.method();
}

}


namespace std {

template<>
struct hash<matrix::Content> {
  size_t operator()(const matrix::Content &c) const {
    return hash_combine(qHash(c.host()), qHash(c.id()));
  }
};

template<>
struct hash<matrix::Thumbnail> {
  size_t operator()(const matrix::Thumbnail &t) const {
    return
      hash_combine(
        hash_combine(
          hash_combine(
            hash<matrix::Content>()(t.content()),
            hash<int>()(t.size().width())),
          hash<int>()(t.size().height())),
        hash<matrix::ThumbnailMethod>()(t.method()));
  }
};

}

#endif
