#include "Content.hpp"

#include <stdexcept>

#include <QUrl>
#include <QStringBuilder>

namespace matrix {

Content::Content(const QUrl &url) {
  if(url.scheme() != "mxc")
    throw std::invalid_argument("content URLs must have scheme \"mxc\"");
  host_ = url.host(QUrl::FullyEncoded);
  id_ = url.path(QUrl::FullyEncoded).remove(0, 1);
  if(url.hasFragment())
    fragment_ = url.fragment(QUrl::FullyEncoded).remove(0, 1);
}

QUrl Content::url() const noexcept {
  QUrl url;
  url.setScheme("mxc");
  url.setHost(host_, QUrl::StrictMode);
  url.setPath("/" + id_, QUrl::StrictMode);
  if(!fragment_.isEmpty())
    url.setFragment(fragment_, QUrl::StrictMode);
  return url;
}

QUrl Content::url_on(const QUrl &homeserver) const noexcept {
  QUrl url = homeserver;
  url.setPath("/_matrix/media/r0/download/" % host_ % "/" % id_, QUrl::StrictMode);
  if(!fragment_.isEmpty())
    url.setFragment(fragment_, QUrl::StrictMode);
  return url;
}

}
