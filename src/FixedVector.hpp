#ifndef NACHAT_FIXED_VECTOR_HPP_
#define NACHAT_FIXED_VECTOR_HPP_

#include <memory>
#include <cstddef>
#include <iterator>

template<typename T>
class FixedVector {
public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = value_type &;
  using const_reference = const value_type &;
  using pointer = T *;
  using const_pointer = const T *;
  using iterator = T *;
  using const_iterator = const T *;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  FixedVector() : size_{0} {}
  explicit FixedVector(size_type size) : size_{size}, data_{new T[size]} {}

  iterator begin() { return data_.get(); }
  iterator end() { return data_.get() + size_; }
  const_iterator begin() const { return data_.get(); }
  const_iterator end() const { return data_.get() + size_; }
  const_iterator cbegin() const { return begin(); }
  const_iterator cend() const { return end(); }
  
  reverse_iterator rbegin() { return std::reverse_iterator<iterator>(begin()); }
  reverse_iterator rend() { return std::reverse_iterator<iterator>(end()); }
  const_reverse_iterator rbegin() const { return std::reverse_iterator<const_iterator>(begin()); }
  const_reverse_iterator rend() const { return std::reverse_iterator<const_iterator>(end()); }
  const_reverse_iterator crbegin() const { return std::reverse_iterator<const_iterator>(begin()); }
  const_reverse_iterator crend() const { return std::reverse_iterator<const_iterator>(end()); }

  reference operator[](size_type i) { return data_[i]; }
  const_reference operator[](size_type i) const { return data_[i]; }

  reference front() { return data_[0]; }
  const_reference front() const { return data_[0]; }

  reference back() { return data_[size_-1]; }
  const_reference back() const { return data_[size_-1]; }

  size_type size() const { return size_; }

private:
  size_type size_;
  std::unique_ptr<T[]> data_;
};

#endif
