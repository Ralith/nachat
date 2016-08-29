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
enum class Membership;
class TimelineManager;
struct UserID;

namespace event {
class Room;
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

  void membership_changed(const matrix::UserID &, matrix::Membership, matrix::Membership);
  void member_name_changed(const matrix::UserID &, QString);
  void topic_changed();
  void command(const QString &name, const QString &args);
};

#endif // ROOMVIEW_H
