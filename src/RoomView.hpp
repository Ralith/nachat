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
class MemberListModel;

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
  matrix::Room &room_;
  matrix::TimelineManager *timeline_manager_;
  matrix::MemberListModel *member_list_;

  void topic_changed();
  void command(const QString &name, const QString &args);
  void send(const matrix::EventType &ty, const matrix::event::Content &content);
  void update_last_read();
};

#endif // ROOMVIEW_H
