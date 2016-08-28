#include <QApplication>

#include "matrix/Room.hpp"

#include "ContentCache.hpp"
#include "TimelineView.hpp"

matrix::event::Room room_evt(const QJsonObject &o) {
  return matrix::event::Room{matrix::event::Identifiable{matrix::Event{o}}};
}

matrix::event::room::Message message_evt(const QJsonObject &o) {
  return matrix::event::room::Message{room_evt(o)};
}

matrix::event::room::Member member_evt(const QJsonObject &o) {
  return matrix::event::room::Member{matrix::event::room::State{room_evt(o)}};
}

int main(int argc, char *argv[]) {
  QApplication a(argc, argv);

  ThumbnailCache c;

  QObject::connect(&c, &ThumbnailCache::needs, [&c](const matrix::Thumbnail &thumb) {
      QPixmap pixmap(thumb.size());
      pixmap.fill(Qt::black);
      c.set(thumb, pixmap);
    });

  TimelineView tv(c);
  tv.show();

  matrix::RoomState rs;
  matrix::TimelineCursor cursor1{"1"};
  const char *somebody = "@somebody:example.com";
  
  auto join_evt = member_evt(QJsonObject{
        {"type", "m.room.member"},
        {"event_id", "2"},
        {"sender", somebody},
        {"origin_server_ts", 42000000LL},
        {"state_key", somebody},
        {"content", QJsonObject{
            {"membership", "join"},
            {"displayname", "SOMEBODY"},
            {"avatar_url", "mxc://example.com/foo.png"}
          }}
    });

  tv.append(cursor1, rs, join_evt);

  rs.apply(join_evt);

  tv.append(cursor1, rs, message_evt(QJsonObject{
        {"type", "m.room.message"},
        {"event_id", "3"},
        {"sender", somebody},
        {"origin_server_ts", 42000001LL},
        {"content", QJsonObject{
            {"body", "hello world https://example.com/ whee\nnew line! https://example.com/\nhttp://example.com/"},
            {"msgtype", "m.text"}
          }}
      }));

  tv.append(cursor1, rs, message_evt(QJsonObject{
        {"type", "m.room.message"},
        {"event_id", "3.1"},
        {"sender", somebody},
        {"origin_server_ts", 42000002LL},
        {"content", QJsonObject{
            {"body", "this will be redacted!"},
            {"msgtype", "m.text"}
          }}
      }));

  matrix::event::room::Redaction redact_evt{room_evt(QJsonObject{
        {"type", "m.room.redaction"},
        {"event_id", "5"},
        {"origin_server_ts", 42000003LL},
        {"redacts", "3.1"},
        {"sender", somebody},
        {"content", QJsonObject{
            {"reason", "idk lol"}
          }}
      })};

  tv.redact(redact_evt);
  tv.append(cursor1, rs, redact_evt);

  auto leave_evt = member_evt(QJsonObject{
        {"type", "m.room.member"},
        {"event_id", "4"},
        {"sender", somebody},
        {"origin_server_ts", 82000002LL},
        {"state_key", somebody},
        {"content", QJsonObject{
            {"membership", "leave"},
          }}
    });

  tv.append(cursor1, rs, leave_evt);

  rs.apply(leave_evt);

  tv.prepend(cursor1, matrix::RoomState(), matrix::event::room::Create{matrix::event::room::State{room_evt(QJsonObject{
            {"type", "m.room.create"},
            {"event_id", "1"},
            {"sender", somebody},
            {"origin_server_ts", 42},
            {"state_key", ""},
            {"content", QJsonObject{
                {"creator", "you"}
              }}
          })}});

  return a.exec();
}
