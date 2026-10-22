#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace training::core::detector {

/// Кольцевой буфер фиксированного размера: элементы читаются в порядке
/// добавления, при переполнении самый старый выбрасывается. Элемент с номером
/// n от первого добавленного после создания или `clear` лежит в ячейке n по
/// модулю размера буфера.
///
/// Многопоточность: все методы можно звать из любых потоков одновременно.
template <class T> class Buffer {
public:
  /// Создаёт пустой буфер на `capacity` элементов. Выделяет память под все
  /// элементы; дальше память не выделяет. Буфер на ноль элементов пуст всегда.
  explicit Buffer(std::size_t capacity) : items_(capacity) {}

  /// Забирает элементы буфера `other` под его замком; `other` остаётся
  /// буфером на ноль элементов.
  Buffer(Buffer &&other) noexcept : Buffer(std::move(other), std::scoped_lock(other.mutex_)) {}

  /// Копии нет: буфер держит его замок.
  Buffer(const Buffer &) = delete;

  /// Присваивания копией нет: буфер держит его замок.
  Buffer &operator=(const Buffer &) = delete;

  /// Забирает элементы буфера `other` под замками обоих; `other` остаётся
  /// буфером на ноль элементов.
  Buffer &operator=(Buffer &&other) noexcept {
    if (this == &other)
      return *this;
    const std::scoped_lock lock(mutex_, other.mutex_);
    items_ = std::move(other.items_);
    other.items_.clear();
    first_ = std::exchange(other.first_, 0);
    count_ = std::exchange(other.count_, 0);
    return *this;
  }

  /// Размер буфера: сколько элементов он помнит.
  std::size_t capacity() const {
    const std::scoped_lock lock(mutex_);
    return items_.size();
  }

  /// Дописывает элемент `item` после всех. В полном буфере самый старый
  /// элемент выбрасывается.
  void push(const T &item) {
    const std::scoped_lock lock(mutex_);
    if (items_.empty())
      return;
    items_[(first_ + count_) % items_.size()] = item;
    if (count_ < items_.size())
      ++count_;
    else
      first_ = (first_ + 1) % items_.size();
  }

  /// Забирает самый старый элемент из буфера.
  ///
  /// @return пусто, если буфер пуст.
  std::optional<T> pop() {
    const std::scoped_lock lock(mutex_);
    if (count_ == 0)
      return std::nullopt;
    const T item = items_[first_];
    first_ = (first_ + 1) % items_.size();
    --count_;
    return item;
  }

  /// Очищает буфер.
  void clear() {
    const std::scoped_lock lock(mutex_);
    first_ = 0;
    count_ = 0;
  }

private:
  /// Забирает элементы буфера `other`; его замок держит вызывающий.
  Buffer(Buffer &&other, const std::scoped_lock<std::mutex> & /*lock*/)
      : items_(std::move(other.items_)), first_(std::exchange(other.first_, 0)),
        count_(std::exchange(other.count_, 0)) {
    other.items_.clear();
  }

  // Элементы — под этим замком.
  mutable std::mutex mutex_;
  std::vector<T> items_;

  // Ячейка самого старого элемента и число элементов.
  std::size_t first_ = 0;
  std::size_t count_ = 0;
};

} // namespace training::core::detector
