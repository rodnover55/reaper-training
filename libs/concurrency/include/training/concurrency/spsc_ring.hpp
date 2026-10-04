#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <optional>
#include <span>
#include <type_traits>

namespace training::concurrency {

/// Кольцевая очередь без блокировок между одним пишущим и одним читающим
/// потоком.
///
/// Пишущая сторона никогда не ждёт, не выделяет память и не зовёт систему,
/// поэтому годится для звукового потока. Если читающий отстал и места нет,
/// запись отбрасывается целиком и считается в `dropped()`.
///
/// Многопоточность: `push` и `freeSpace` зовёт только пишущий поток, `pop` —
/// только читающий; оба могут работать одновременно. Писать по очереди могут и
/// разные потоки, если сами они упорядочены, — то же для чтения.
///
/// Производительность: объект хранит все `Capacity` элементов внутри себя.
/// Большую очередь стоит создавать в куче.
///
/// @tparam T тип элемента; копируется побайтно.
/// @tparam Capacity число мест; степень двойки.
template <class T, std::size_t Capacity> class SpscRing {
  static_assert(std::has_single_bit(Capacity), "ёмкость — степень двойки");
  static_assert(std::is_trivially_copyable_v<T>,
                "в звуковом потоке — только простое копирование");

public:
  /// Кладёт один элемент в конец очереди. Зовётся только пишущим потоком.
  ///
  /// @return ложь, если места нет: элемент отброшен, `dropped()` вырос на 1.
  bool push(const T &value) noexcept {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    if (head - tail_.load(std::memory_order_acquire) >= Capacity) {
      dropped_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }

    slots_[head & (Capacity - 1)] = value;
    head_.store(head + 1, std::memory_order_release);
    return true;
  }

  /// Кладёт все элементы `values` в конец очереди одним шагом: читающий
  /// увидит их только все сразу. Зовётся только пишущим потоком.
  ///
  /// @return ложь, если места на все не хватает: не кладётся ни один,
  ///   `dropped()` вырос на 1. Пустой `values` всегда кладётся.
  bool push(std::span<const T> values) noexcept {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    if (values.size() > Capacity - (head - tail_.load(std::memory_order_acquire))) {
      dropped_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }

    for (std::size_t i = 0; i < values.size(); ++i)
      slots_[(head + i) & (Capacity - 1)] = values[i];

    head_.store(head + values.size(), std::memory_order_release);
    return true;
  }

  /// Сколько элементов можно положить прямо сейчас. Зовётся только пишущим
  /// потоком: для него значение не уменьшится до его следующего `push`.
  std::size_t freeSpace() const noexcept {
    return Capacity -
           (head_.load(std::memory_order_relaxed) - tail_.load(std::memory_order_acquire));
  }

  /// Забирает первый элемент очереди. Зовётся только читающим потоком.
  ///
  /// @return элемент или пусто, если очередь пуста.
  std::optional<T> pop() noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire))
      return std::nullopt;

    const T value = slots_[tail & (Capacity - 1)];
    tail_.store(tail + 1, std::memory_order_release);
    return value;
  }

  /// Забирает из начала очереди столько элементов, сколько есть, но не больше
  /// `out.size()`, и пишет их в начало `out`. Остальная часть `out` не
  /// меняется. Зовётся только читающим потоком.
  ///
  /// @return сколько элементов записано в `out`; 0 — очередь пуста.
  std::size_t pop(std::span<T> out) noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t available = head_.load(std::memory_order_acquire) - tail;
    const std::size_t count = available < out.size() ? available : out.size();

    for (std::size_t i = 0; i < count; ++i)
      out[i] = slots_[(tail + i) & (Capacity - 1)];

    tail_.store(tail + count, std::memory_order_release);
    return count;
  }

  /// Сколько раз `push` отказал из-за нехватки места — за всё время жизни
  /// очереди. Можно читать из любого потока.
  std::size_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
  // Счётчики пишущего и читающего — на разных строках кэша, между ними
  // записи: так стороны не мешают друг другу.
  alignas(64) std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> dropped_{0};
  std::array<T, Capacity> slots_{};
  alignas(64) std::atomic<std::size_t> tail_{0};
};

} // namespace training::concurrency
