#ifndef ROOMVIEW_H
#define ROOMVIEW_H

#include <experimental/optional>

#include <QWidget>

#include "QStringHash.hpp"

namespace Ui {
class RoomView;
}

namespace matrix {
class Room;
class RoomState;
enum class Membership;
class TimelineManager;
class UserID;
class EventType;

namespace event {
class Room;
class Content;

namespace room {
class MemberContent;
}
}
}

class TimelineView;
class EntryBox;
class MemberList;
class ThumbnailCache;

class RoomView : public QWidget
{
  Q_OBJECT

public:
  explicit RoomView(ThumbnailCache &cache, matrix::Room &room, QWidget *parent = nullptr);
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
  matrix::TimelineManager *timeline_manager_;

  void member_changed(const matrix::UserID &member, const matrix::event::room::MemberContent &old, const matrix::event::room::MemberContent &current);
  void member_disambiguation_changed(const matrix::UserID &member,
                                     const std::experimental::optional<QString> &old, const std::experimental::optional<QString> &current);
  void topic_changed();
  void command(const QString &name, const QString &args);
  void send(const matrix::EventType &ty, const matrix::event::Content &content);
};

#endif // ROOMVIEW_H
