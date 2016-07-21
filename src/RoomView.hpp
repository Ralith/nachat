#ifndef ROOMVIEW_H
#define ROOMVIEW_H

#include <map>

#include <QWidget>

#include "QStringHash.hpp"

namespace Ui {
class RoomView;
}

namespace matrix {
class Room;
class RoomState;
class Member;
enum class Membership;

namespace proto {
struct Event;
}
}

class TimelineView;
class EntryBox;

class RoomView : public QWidget
{
  Q_OBJECT

public:
  explicit RoomView(matrix::Room &room, QWidget *parent = nullptr);
  ~RoomView();

  const matrix::Room &room() { return room_; }

private:
  Ui::RoomView *ui;
  TimelineView *timeline_view_;
  EntryBox *entry_;
  matrix::Room &room_;

  class Compare {
  public:
    bool operator()(const QString &a, const QString &b) const { return key(a) < key(b); }

  private:
    static QString key(const QString &n);
  };

  std::map<QString, const matrix::Member *, Compare> member_list_;

  void message(const matrix::proto::Event &);
  void membership_changed(const matrix::Member &, matrix::Membership);
  void member_name_changed(const matrix::Member &, QString);
  void topic_changed(const QString &);
  void update_members();
  void append_message(const matrix::RoomState &, const matrix::proto::Event &);
};

#endif // ROOMVIEW_H
