#ifndef NATIVE_CHAT_MATRIX_ID_HPP_
#define NATIVE_CHAT_MATRIX_ID_HPP_

#include <QString>

#include "hash.hpp"

namespace matrix {

enum class Direction { FORWARD, BACKWARD };

template<typename T>
class ID {
public:
  explicit ID(T value) : s(std::move(value)) {}
  explicit operator const T &() const noexcept { return s; }
  explicit operator T &() noexcept { return s; }
  T &value() noexcept { return s; }
  const T &value() const noexcept { return s; }
private:
  T s;
};

template<typename T> inline bool operator==(const ID<T> &x, const ID<T> &y) noexcept { return x.value() == y.value(); }
template<typename T> inline bool operator!=(const ID<T> &x, const ID<T> &y) noexcept { return x.value() != y.value(); }
template<typename T> inline bool operator<(const ID<T> &x, const ID<T> &y) noexcept { return x.value() < y.value(); }

class TransactionID : public ID<QString> { using ID::ID; };

class TimelineCursor : public ID<QString> { using ID::ID; };
class SyncCursor : public ID<QString> { using ID::ID; };

class EventID : public ID<QString> { using ID::ID; };
class RoomID : public ID<QString> { using ID::ID; };

class EventType : public ID<QString> { using ID::ID; };
class MessageType : public ID<QString> { using ID::ID; };

class StateKey : public ID<QString> { using ID::ID; };

class UserID : public ID<QString> {
  using ID::ID;
  explicit UserID(const StateKey &key) : ID(key.value()) {}
};

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
