#ifndef NATIVE_CHAT_MATRIX_CONTENT_HPP_
#define NATIVE_CHAT_MATRIX_CONTENT_HPP_

#include <QString>
#include <QHash>

class QUrl;

namespace matrix {

class Content {
public:
  Content(const QString &host, const QString &id, const QString &fragment = "") noexcept : host_(host), id_(id), fragment_(fragment) {}
  explicit Content(const QUrl &url);

  const QString &host() const noexcept { return host_; }
  const QString &id() const noexcept { return id_; }
  const QString &fragment() const noexcept { return fragment_; }

  QUrl url() const noexcept;
  QUrl url_on(const QUrl &homeserver) const noexcept;

private:
  QString host_, id_, fragment_;
};

inline bool operator==(const Content &a, const Content &b) {
  return a.host() == b.host() && a.id() == b.id() && a.fragment() == b.fragment();
}

}


namespace std {

template<>
struct hash<matrix::Content> {
  size_t operator()(const matrix::Content &c) const {
    return (qHash(c.host()) << 2) ^ (qHash(c.id()) << 1) ^ qHash(c.fragment());
  }
};

}

#endif
