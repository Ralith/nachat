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
class MemberList;

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
  MemberList *member_list_;
  matrix::Room &room_;

  void message(const matrix::proto::Event &);
  void membership_changed(const matrix::Member &, matrix::Membership);
  void member_name_changed(const matrix::Member &, QString);
  void topic_changed(const QString &);
  void append_message(const matrix::RoomState &, const matrix::proto::Event &);
};

#endif // ROOMVIEW_H
