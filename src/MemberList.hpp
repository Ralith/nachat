#ifndef NATIVE_CLIENT_MEMBER_LIST_HPP_
#define NATIVE_CLIENT_MEMBER_LIST_HPP_

#include <map>
#include <experimental/optional>

#include <QListWidget>

#include "matrix/ID.hpp"

namespace matrix {
class RoomState;
namespace event {
namespace room {
class MemberContent;
}
}
}

class MemberList : public QListWidget {
public:
  MemberList(const matrix::RoomState &, QWidget *parent = nullptr);

  void member_changed(const matrix::RoomState &, const matrix::UserID &,
                      const matrix::event::room::MemberContent &old, const matrix::event::room::MemberContent &current);
  void member_disambiguation_changed(const matrix::RoomState &, const matrix::UserID &,
                                     const std::experimental::optional<QString> &old, const std::experimental::optional<QString> &current);

  QSize sizeHint() const override;

private:
  class Compare {
  public:
    bool operator()(const QString &a, const QString &b) const { return key(a) < key(b); }

  private:
    static QString key(const QString &n);
  };

  std::map<QString, const matrix::UserID, Compare> members_;
  QSize size_hint_;             // cache to avoid recomputing

  void update_members();
};

#endif
