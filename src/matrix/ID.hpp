#ifndef NATIVE_CHAT_MATRIX_ID_HPP_
#define NATIVE_CHAT_MATRIX_ID_HPP_

#include <QString>

#include "hash.hpp"

namespace matrix {

enum class Direction { FORWARD, BACKWARD };

class ID {
public:
  explicit ID(QString value) : s(std::move(value)) {}
  explicit operator const QString &() const noexcept { return s; }
  explicit operator QString &() noexcept { return s; }
  QString &value() noexcept { return s; }
  const QString &value() const noexcept { return s; }
private:
  QString s;
};

inline bool operator==(const ID &x, const ID &y) noexcept { return x.value() == y.value(); }
inline bool operator!=(const ID &x, const ID &y) noexcept { return x.value() != y.value(); }
inline bool operator<(const ID &x, const ID &y) noexcept { return x.value() < y.value(); }

struct TimelineCursor : public ID { using ID::ID; };
struct SyncCursor : public ID { using ID::ID; };

struct EventID : public ID { using ID::ID; };
struct RoomID : public ID { using ID::ID; };
struct UserID : public ID { using ID::ID; };

struct EventType : public ID { using ID::ID; };
struct MessageType : public ID { using ID::ID; };

struct StateKey : public ID { using ID::ID; };

struct StateID {
  EventType type;
  StateKey key;

  StateID(EventType type, StateKey key) : type(type), key(key) {}
};

}

namespace std {

template<>
struct hash<matrix::EventID> {
  size_t operator()(const matrix::EventID &id) const {
    return qHash(id.value());
  }
};

template<>
struct hash<matrix::RoomID> {
  size_t operator()(const matrix::RoomID &id) const {
    return qHash(id.value());
  }
};

template<>
struct hash<matrix::UserID> {
  size_t operator()(const matrix::UserID &id) const {
    return qHash(id.value());
  }
};

template<>
struct hash<matrix::EventType> {
  size_t operator()(const matrix::EventType &id) const {
    return qHash(id.value());
  }
};

template<>
struct hash<matrix::StateKey> {
  size_t operator()(const matrix::StateKey &id) const {
    return qHash(id.value());
  }
};

template<>
struct hash<matrix::StateID> {
  size_t operator()(const matrix::StateID &e) const {
    return hash_combine(std::hash<matrix::EventType>()(e.type), e.key);
  }
};

}

#endif
