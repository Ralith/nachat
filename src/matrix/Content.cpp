#include "Content.hpp"

#include <stdexcept>

#include <QUrl>
#include <QStringBuilder>

namespace matrix {

Content::Content(const QUrl &url) {
  if(url.scheme() != "mxc")
    throw std::invalid_argument("content URLs must have scheme \"mxc\"");
  host_ = url.host(QUrl::FullyDecoded);
  id_ = url.path(QUrl::FullyDecoded).remove(0, 1);
  if(url.hasFragment())
    fragment_ = url.fragment(QUrl::FullyEncoded).remove(0, 1);
}

QUrl Content::url() const noexcept {
  QUrl url;
  url.setScheme("mxc");
  url.setHost(host_);
  url.setPath("/" + QUrl::toPercentEncoding(id_), QUrl::StrictMode);
  if(!fragment_.isEmpty())
    url.setFragment(fragment_, QUrl::StrictMode);
  return url;
}

QUrl Content::url_on(const QUrl &homeserver) const noexcept {
  QUrl url = homeserver;
  url.setPath(QString("/_matrix/media/r0/download/" % QUrl::toPercentEncoding(host_) % "/" % QUrl::toPercentEncoding(id_)), QUrl::StrictMode);
  if(!fragment_.isEmpty())
    url.setFragment(fragment_, QUrl::StrictMode);
  return url;
}

}
