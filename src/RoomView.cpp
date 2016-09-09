#include "RoomView.hpp"
#include "ui_RoomView.h"

#include <stdexcept>
#include <experimental/optional>

#include <QGuiApplication>
#include <QCursor>
#include <QScrollBar>
#include <QDebug>

#include "matrix/Room.hpp"
#include "matrix/Session.hpp"
#include "matrix/TimelineWindow.hpp"

#include "TimelineView.hpp"
#include "EntryBox.hpp"
#include "RoomMenu.hpp"
#include "MemberList.hpp"

using std::experimental::optional;

RoomView::RoomView(ThumbnailCache &cache, matrix::Room &room, QWidget *parent)
  : QWidget(parent), ui(new Ui::RoomView),
    timeline_view_(new TimelineView(room.session().homeserver(), cache, this)),
    entry_(new EntryBox(this)), member_list_(new MemberList(room.state(), this)),
    room_(room),
    timeline_manager_{new matrix::TimelineManager(room, this)} {
  ui->setupUi(this);

  connect(timeline_manager_, &matrix::TimelineManager::grew,
          [this](matrix::Direction dir, const matrix::TimelineCursor &begin, const matrix::RoomState &state, const matrix::event::Room &evt) {
            if(dir == matrix::Direction::BACKWARD) {
              timeline_view_->prepend(begin, state, evt);
            } else {
              timeline_view_->append(begin, state, evt);
              timeline_view_->set_at_bottom(timeline_manager_->window().at_end());
            }
          });
  connect(timeline_manager_, &matrix::TimelineManager::discontinuity, [this]() {
      timeline_view_->set_at_bottom(false);
    });

  connect(timeline_view_, &TimelineView::need_backwards, [this]() { timeline_manager_->grow(matrix::Direction::BACKWARD); });
  connect(timeline_view_, &TimelineView::need_forwards, [this]() { timeline_manager_->grow(matrix::Direction::FORWARD); });
  connect(timeline_view_, &TimelineView::redact_requested, &room, &matrix::Room::redact); // TODO: Add to timeline_view_'s pending events
  connect(timeline_view_, &TimelineView::event_read, &room, &matrix::Room::send_read_receipt);
  connect(&room, &matrix::Room::receipts_changed, this, &RoomView::update_last_read);

  // Ensure redactions apply instantly even when the view is scrolled back and therefore not receiving sync events.
  connect(&room, &matrix::Room::redaction, timeline_view_, &TimelineView::redact);

  timeline_manager_->replay();
  timeline_view_->set_at_bottom(timeline_manager_->window().at_end());

  auto menu = new RoomMenu(room, this);
  connect(ui->menu_button, &QAbstractButton::clicked, [this, menu](bool) {
      menu->popup(QCursor::pos());
    });

  ui->central_splitter->insertWidget(0, timeline_view_);
  ui->central_splitter->setCollapsible(0, false);
  ui->central_splitter->insertWidget(1, member_list_);

  ui->layout->insertWidget(2, entry_);
  setFocusProxy(entry_);
  connect(entry_, &EntryBox::message, [this](const QString &msg) {
      send(matrix::event::room::Message::tag(),
           matrix::event::Content{{
               {"msgtype", "m.text"},
               {"body", msg}}});
    });
  connect(entry_, &EntryBox::command, this, &RoomView::command);
  connect(entry_, &EntryBox::pageUp, [this]() {
      timeline_view_->verticalScrollBar()->triggerAction(QAbstractSlider::SliderPageStepSub);
    });
  connect(entry_, &EntryBox::pageDown, [this]() {
      timeline_view_->verticalScrollBar()->triggerAction(QAbstractSlider::SliderPageStepAdd);
    });
  connect(entry_, &EntryBox::activity, timeline_view_, &TimelineView::mark_read);

  connect(&room_, &matrix::Room::member_changed, this, &RoomView::member_changed);
  connect(&room_, &matrix::Room::member_disambiguation_changed, this, &RoomView::member_disambiguation_changed);

  connect(&room_, &matrix::Room::topic_changed, this, &RoomView::topic_changed);
  topic_changed();
}

RoomView::~RoomView() { delete ui; }

void RoomView::member_disambiguation_changed(const matrix::UserID &member, const optional<QString> &old, const optional<QString> &current) {
  member_list_->member_disambiguation_changed(room_.state(), member, old, current);
}

void RoomView::member_changed(const matrix::UserID &member,
                              const matrix::event::room::MemberContent &old,
                              const matrix::event::room::MemberContent &current) {
  member_list_->member_changed(room_.state(), member, old, current);
}

void RoomView::topic_changed() {
  if(!room_.state().topic()) {
    ui->topic->setTextFormat(Qt::RichText);
    ui->topic->setText("<h2>" + room_.pretty_name() + "</h2>");
  } else {
    ui->topic->setTextFormat(Qt::PlainText);
    ui->topic->setText(*room_.state().topic());
  }
}

void RoomView::command(const QString &name, const QString &args) {
  if(name == "me") {
    send(matrix::event::room::Message::tag(),
         matrix::event::Content{{
             {{"msgtype", "m.emote"},
               {"body", args}}}});
  } else if(name == "join") {
    auto req = room_.session().join(args);
    connect(req, &matrix::JoinRequest::error, [=](const QString &msg) { qCritical() << tr("failed to join \"%1\": %2").arg(args).arg(msg); });
  } else {
    qCritical() << tr("Unrecognized command: %1").arg(name);
  }
}

void RoomView::selected() {
  // TODO: Mark events read
}

void RoomView::send(const matrix::EventType &ty, const matrix::event::Content &content) {
  timeline_view_->add_pending(room_.send(ty, content), room_.state(), room_.session().user_id(),
                              std::chrono::time_point_cast<Time::duration>(std::chrono::system_clock::now()), ty, content);
}

void RoomView::update_last_read() {
  auto r = room_.receipt_from(room_.session().user_id());
  if(!r) return;
  timeline_view_->set_last_read(r->event);
}
