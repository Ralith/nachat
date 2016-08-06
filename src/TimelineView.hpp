#ifndef NATIVE_CHAT_TIMELINE_VIEW_HPP_
#define NATIVE_CHAT_TIMELINE_VIEW_HPP_

#include <deque>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <experimental/optional>
#include <functional>

#include <QAbstractScrollArea>
#include <QTextLayout>
#include <QIcon>
#include <QPixmap>

#include "matrix/Event.hpp"
#include "matrix/Room.hpp"
#include "matrix/Content.hpp"

#include "QStringHash.hpp"
#include "EventView.hpp"

class TimelineView : public QAbstractScrollArea {
  Q_OBJECT

public:
  TimelineView(matrix::Room &room, QWidget *parent = nullptr);

  void end_batch(const QString &token);
  // Call upon receiving a new batch, with that batch's prev_batch token.

  void push_back(const matrix::RoomState &state, const matrix::proto::Event &e);

  void reset();
  // Call if a gap arises in events

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

  BlockRenderInfo block_info() const;

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void focusOutEvent(QFocusEvent *event) override;
  void contextMenuEvent(QContextMenuEvent *event) override;
  bool viewportEvent(QEvent *event) override;

private:
  struct Batch {
    std::deque<Event> events;  // deque purely so we don't try to copy/move QTextLayout
    QString token;

    size_t size() const;
  };

  struct Avatar {
    size_t references = 0;
    QPixmap pixmap;
  };

  struct Selection {
    Block *start;               // Point where selection began
    QPointF start_pos;           // relative to start origin
    Block *end;                  // Point where selection completed
    QPointF end_pos;             // relative to end origin
  };

  struct VisibleBlock {
    Block *block;
    QRectF bounds;               // relative to view
  };

  class SelectionScanner {
  public:
    SelectionScanner(const std::experimental::optional<Selection> &s) : s_(s) {}

    void advance(const Block *b) {
      inside_selection_ &= !ending_selection();
      starting_ = false;
      ending_ = false;
      check_starting(b);
    }

    bool on_selection_edge(const Block *b) const { return s_ && (b == s_->start || b == s_->end); }
    bool starting_selection() const { return starting_; }
    bool ending_selection() const { return ending_; }
    bool fully_selected(const Block *b) const { return inside_selection_ && !on_selection_edge(b); }
    std::experimental::optional<QPointF> start_point() const {
      return starting_selection()
        ? std::experimental::optional<QPointF>(selection_starts_at_start_ ? s_->start_pos : s_->end_pos)
        : std::experimental::optional<QPointF>();
    }
    std::experimental::optional<QPointF> end_point() const {
      return ending_selection()
        ? std::experimental::optional<QPointF>(selection_starts_at_start_ ? s_->end_pos : s_->start_pos)
        : std::experimental::optional<QPointF>();
    }

  private:
    const std::experimental::optional<Selection> &s_;

    bool inside_selection_ = false;
    bool selection_starts_at_start_;

    bool starting_ = false;
    bool ending_ = false;

    void check_starting(const Block *b) {
      starting_ = !inside_selection_ && on_selection_edge(b);
      ending_ = on_selection_edge(b) && (inside_selection_ || s_->start == s_->end);
      inside_selection_ |= starting_;
      if(starting_) selection_starts_at_start_ = b == s_->start;
    }
  };

  matrix::Room &room_;
  matrix::RoomState initial_state_;
  std::deque<Batch> batches_;  // deque so we can add/remote batches from either end
  std::deque<Block> blocks_;   // what actually gets drawn
  size_t total_events_;
  bool head_color_alternate_;
  bool backlog_growing_;
  bool backlog_growable_;  // false iff we've found the room create message
  bool backlog_grow_cancelled_;  // true iff we reset since backlog started growing
  size_t min_backlog_size_;
  qreal content_height_;
  QString prev_batch_;  // Token for the batch immediately prior to the first message
  std::unordered_map<matrix::Content, Avatar> avatars_;
  QIcon avatar_unset_, avatar_loading_;
  std::experimental::optional<Selection> selection_;
  QShortcut *copy_;
  std::vector<VisibleBlock> visible_blocks_;
  QPixmap spinner_;
  Block *grabbed_focus_;

  void update_scrollbar(bool grew_upward);

  void grow_backlog();
  void prepend_batch(QString start, QString end, gsl::span<const matrix::proto::Event> events);
  void backlog_grow_error();
  int scrollback_trigger_size() const;
  int scrollback_status_size() const;
  void set_avatar(const matrix::Content &content, const QString &type, const QString &disposition, const QByteArray &data);
  void unref_avatar(const matrix::Content &);
  void copy();
  void pop_front_block();
  QString selection_text() const;
  VisibleBlock *dispatch_event(const std::experimental::optional<QPointF> &p, QEvent *e);

  VisibleBlock *block_near(const QPointF &p);   // Point relative to view

  void prune_backlog();
  // Removes enough blocks from the backlog that calling for each new event will cause backlog size to approach one
  // batch size greater than min_backlog_size_. Requires but does not perform scrollbar update!

  void ref_avatar(const matrix::Content &);
};

#endif
