#ifndef NACHAT_FIXED_VECTOR_HPP_
#define NACHAT_FIXED_VECTOR_HPP_

#include <memory>
#include <cstddef>
#include <iterator>
#include <type_traits>

template<typename T>
class FixedVector {
private:
  using storage_type = std::aligned_storage_t<sizeof(T), alignof(T)>;

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

  explicit FixedVector(size_type capacity) : size_{0}, capacity_{capacity}, data_{new storage_type[capacity]} {}
  FixedVector() noexcept : FixedVector{0} {}
  ~FixedVector() noexcept(std::is_nothrow_destructible<T>::value) {
    for(auto &x : *this) {
      x.~T();
    }
  }

  FixedVector(FixedVector &&other) noexcept : size_{other.size}, capacity_{other.capacity}, data_{std::move(other.data_)} {
    other.size_ = 0;
    other.capacity_ = 0;
  }

  FixedVector &operator=(FixedVector &&other) {
    size_ = other.size_;
    capacity_ = other.capacity_;
    data_ = std::move(other.data_);
    other.size_ = 0;
    other.capacity_ = 0;
    return *this;
  }

  template<typename ...Ts>
  void emplace_back(Ts &&...ts) noexcept(std::is_nothrow_constructible<T, Ts...>::value) {
    new (data_.get() + size_) T(std::forward<Ts>(ts)...);
    ++size_;
  }

  void pop_back() noexcept(std::is_nothrow_destructible<T>::value) {
    (*this)[size_ - 1].~T();
    --size_;
  }

  iterator begin() noexcept { return reinterpret_cast<pointer>(data_.get()); }
  iterator end() noexcept { return reinterpret_cast<pointer>(data_.get()) + size_; }
  const_iterator begin() const noexcept { return reinterpret_cast<const_pointer>(data_.get()); }
  const_iterator end() const noexcept { return reinterpret_cast<const_pointer>(data_.get()) + size_; }
  const_iterator cbegin() const noexcept { return begin(); }
  const_iterator cend() const noexcept { return end(); }

  reverse_iterator rbegin() noexcept { return std::reverse_iterator<iterator>(end()); }
  reverse_iterator rend() noexcept { return std::reverse_iterator<iterator>(begin()); }
  const_reverse_iterator rbegin() const noexcept { return std::reverse_iterator<const_iterator>(end()); }
  const_reverse_iterator rend() const noexcept { return std::reverse_iterator<const_iterator>(begin()); }
  const_reverse_iterator crbegin() const noexcept { return rbegin(); }
  const_reverse_iterator crend() const noexcept { return rend(); }

  reference operator[](size_type i) noexcept { return reinterpret_cast<reference>(data_[i]); }
  const_reference operator[](size_type i) const noexcept { return reinterpret_cast<const_reference>(data_[i]); }

  reference front() noexcept { return reinterpret_cast<reference>(data_[0]); }
  const_reference front() const noexcept { return reinterpret_cast<const_reference>(data_[0]); }

  reference back() noexcept { return reinterpret_cast<reference>(data_[size_-1]); }
  const_reference back() const noexcept { return reinterpret_cast<const_reference>(data_[size_-1]); }

  size_type size() const noexcept { return size_; }
  size_type capacity() const noexcept { return capacity_; }

private:
  size_type size_, capacity_;
  std::unique_ptr<storage_type[]> data_;
};

#endif
