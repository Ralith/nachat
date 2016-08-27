#ifndef NACHAT_MATRIX_HASH_COMBINE_HPP_
#define NACHAT_MATRIX_HASH_COMBINE_HPP_

#include <cstdint>

inline std::uint64_t hash_combine(std::uint64_t x, std::uint64_t y) {
  static constexpr std::uint64_t factor = 0x9ddfea08eb382d69ULL;
  std::uint64_t a = (y ^ x) * factor;
  a ^= (a >> 47);
  std::uint64_t b = (x ^ a) * factor;
  b ^= (b >> 47);
  return b * factor;
}

#endif
