#include "Content.hpp"

#include <stdexcept>

#include <QUrl>
#include <QUrlQuery>
#include <QStringBuilder>

namespace matrix {

Content::Content(const QUrl &url) {
  if(url.scheme() != "mxc")
    throw illegal_content_scheme();
  host_ = url.host(QUrl::FullyDecoded);
  id_ = url.path(QUrl::FullyDecoded).remove(0, 1);
}

QUrl Content::url() const noexcept {
  QUrl url;
  url.setScheme("mxc");
  url.setHost(host_);
  url.setPath("/" + QUrl::toPercentEncoding(id_), QUrl::StrictMode);
  return url;
}

QUrl Content::url_on(const QUrl &homeserver) const noexcept {
  QUrl url = homeserver;
  url.setPath(QString("/_matrix/media/r0/download/" % QUrl::toPercentEncoding(host_) % "/" % QUrl::toPercentEncoding(id_)), QUrl::StrictMode);
  return url;
}

QUrl Thumbnail::url_on(const QUrl &homeserver) const {
  QUrl url = homeserver;
  QUrlQuery query;
  query.addQueryItem("width", QString::number(size().width()));
  query.addQueryItem("height", QString::number(size().height()));
  query.addQueryItem("method", method() == ThumbnailMethod::SCALE ? "scale" : "crop");
  url.setQuery(std::move(query));
  url.setPath(QString("/_matrix/media/r0/thumbnail/" % content().host() % "/" % content().id()));
  return url;
}

}
