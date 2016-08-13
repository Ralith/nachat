#ifndef ROOMVIEW_H
#define ROOMVIEW_H

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

namespace event {
class Room;
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

  const matrix::Room &room() const { return room_; }
  matrix::Room &room() { return room_; }

  void selected();
  // Notify that user action has brought the room into view. Triggers read receipts.

private:
  Ui::RoomView *ui;
  TimelineView *timeline_view_;
  EntryBox *entry_;
  MemberList *member_list_;
  matrix::Room &room_;

  void message(const matrix::event::Room &);
  void membership_changed(const matrix::Member &);
  void member_name_changed(const matrix::Member &, QString);
  void topic_changed();
  void append_message(const matrix::RoomState &, const matrix::event::Room &);
  void command(const QString &name, const QString &args);
};

#endif // ROOMVIEW_H
