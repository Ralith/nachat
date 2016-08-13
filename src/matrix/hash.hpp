#ifndef NATIVE_CHAT_MATRIX_HASH_HPP_
#define NATIVE_CHAT_MATRIX_HASH_HPP_

#include <functional>

template<typename T>
inline std::size_t hash_combine(std::size_t seed, const T& v) {
  std::hash<T> hasher;
  constexpr std::size_t factor = 0x9ddfea08eb382d69ULL;
  std::size_t a = (hasher(v) ^ seed) * factor;
  a ^= (a >> 47);
  std::size_t b = (seed ^ a) * factor;
  b ^= (b >> 47);
  return b * factor;
}

#endif
