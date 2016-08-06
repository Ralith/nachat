#include "RoomView.hpp"
#include "ui_RoomView.h"

#include <stdexcept>

#include <QGuiApplication>
#include <QCursor>
#include <QScrollBar>
#include <QDebug>

#include "matrix/Room.hpp"
#include "matrix/Member.hpp"
#include "matrix/Session.hpp"

#include "TimelineView.hpp"
#include "EntryBox.hpp"
#include "RoomMenu.hpp"
#include "MemberList.hpp"

RoomView::RoomView(matrix::Room &room, QWidget *parent)
  : QWidget(parent), ui(new Ui::RoomView),
    timeline_view_(new TimelineView(room, this)), entry_(new EntryBox(this)), member_list_(new MemberList(room.state(), this)),
    room_(room) {
  ui->setupUi(this);

  auto menu = new RoomMenu(room, this);
  connect(ui->menu_button, &QAbstractButton::clicked, [this, menu](bool) {
      menu->popup(QCursor::pos());
    });

  ui->central_splitter->insertWidget(0, timeline_view_);
  ui->central_splitter->setCollapsible(0, false);
  ui->central_splitter->insertWidget(1, member_list_);

  ui->layout->insertWidget(2, entry_);
  setFocusProxy(entry_);
  connect(entry_, &EntryBox::message, &room_, &matrix::Room::send_message);
  connect(entry_, &EntryBox::command, this, &RoomView::command);
  connect(entry_, &EntryBox::pageUp, [this]() {
      timeline_view_->verticalScrollBar()->triggerAction(QAbstractSlider::SliderPageStepSub);
    });
  connect(entry_, &EntryBox::pageDown, [this]() {
      timeline_view_->verticalScrollBar()->triggerAction(QAbstractSlider::SliderPageStepAdd);
    });

  connect(&room_, &matrix::Room::message, this, &RoomView::message);
  connect(&room_, &matrix::Room::error, [this](const QString &message) {
      // TODO: UI feedback
      qDebug() << "ERROR:" << room_.pretty_name() << message;
    });

  connect(&room_, &matrix::Room::membership_changed, this, &RoomView::membership_changed);
  connect(&room_, &matrix::Room::member_name_changed, this, &RoomView::member_name_changed);

  connect(&room_, &matrix::Room::discontinuity, timeline_view_, &TimelineView::reset);
  connect(&room_, &matrix::Room::prev_batch, timeline_view_, &TimelineView::end_batch);

  auto replay_state = room_.initial_state();
  for(const auto &batch : room_.buffer()) {
    timeline_view_->end_batch(batch.prev_batch);
    for(const auto &event : batch.events) {
      replay_state.apply(event);
      append_message(replay_state, event);
      replay_state.prune_departed();
    }
  }

  connect(&room_, &matrix::Room::topic_changed, this, &RoomView::topic_changed);
  topic_changed("");
}

RoomView::~RoomView() { delete ui; }

void RoomView::message(const matrix::proto::Event &evt) {
  append_message(room_.state(), evt);
}

void RoomView::member_name_changed(const matrix::Member &member, QString old) {
  member_list_->member_display_changed(room_.state(), member, old);
}

void RoomView::membership_changed(const matrix::Member &member, matrix::Membership old) {
  (void)old;
  member_list_->membership_changed(room_.state(), member);
}

void RoomView::append_message(const matrix::RoomState &state, const matrix::proto::Event &msg) {
  timeline_view_->push_back(state, msg);
}

void RoomView::topic_changed(const QString &old) {
  (void)old;
  if(room_.state().topic().isEmpty()) {
    ui->topic->setTextFormat(Qt::RichText);
    ui->topic->setText("<h2>" + room_.pretty_name() + "</h2>");
  } else {
    ui->topic->setTextFormat(Qt::PlainText);
    ui->topic->setText(room_.state().topic());
  }
}

void RoomView::command(const QString &name, const QString &args) {
  if(name == "me") {
    room_.send_emote(args);
  } else if(name == "join") {
    auto req = room_.session().join(args);
    connect(req, &matrix::JoinRequest::error, timeline_view_, &TimelineView::push_error);
  } else {
    timeline_view_->push_error(tr("Unrecognized command: %1").arg(name));
  }
}
