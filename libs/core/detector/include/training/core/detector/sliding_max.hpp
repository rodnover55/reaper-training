#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace training::core::detector {

/// Значение для вычисления максимума: `value()` даёт число, по которому
/// `SlidingMax` сравнивает элементы.
template <class T>
concept Value = std::copyable<T> && requires(const T &item) {
  { item.value() } -> std::convertible_to<double>;
};

/// Наибольшее значение сэмпла за последние `capacity` сэмплов потока —
/// монотонная очередь в кольце фиксированной длины. Обработка сэмпла стоит в
/// среднем O(1), память выделяется один раз, при создании.
///
/// В очереди лежат кандидаты в максимум: номера возрастают, значения убывают
/// или равны, голова — максимум окна. Значение, за которым пришло строго
/// большее, уходит сразу: дальше в окне всегда есть большее; из равных
/// остаётся самое раннее.
///
/// Многопоточность: все методы можно звать из любых потоков одновременно.
template <Value T> class SlidingMax {
public:
  /// Значение `value` на сэмпле с номером `index`.
  struct SampleValue {
    std::uint64_t index = 0;
    T value{};
  };

  /// Создаёт пустое окно на `capacity` сэмплов.
  ///
  /// @return пусто, если `capacity` — ноль.
  static std::optional<SlidingMax> create(std::size_t capacity) {
    if (capacity == 0)
      return std::nullopt;
    return SlidingMax(capacity);
  }

  /// Забирает окно `other` под его замком; `other` остаётся пустым окном той
  /// же длины.
  SlidingMax(SlidingMax &&other) noexcept
      : SlidingMax(std::move(other), std::scoped_lock(other.mutex_)) {}

  /// Копии нет: окно держит его замок.
  SlidingMax(const SlidingMax &) = delete;

  /// Присваивания копией нет: окно держит его замок.
  SlidingMax &operator=(const SlidingMax &) = delete;

  /// Забирает окно `other` под замками обоих; `other` остаётся пустым окном
  /// той же длины.
  SlidingMax &operator=(SlidingMax &&other) noexcept {
    if (this == &other)
      return *this;
    const std::scoped_lock lock(mutex_, other.mutex_);
    items_ = other.items_;
    head_ = std::exchange(other.head_, 0);
    count_ = std::exchange(other.count_, 0);
    return *this;
  }

  /// Сколько последних сэмплов входит в окно.
  std::size_t capacity() const {
    const std::scoped_lock lock(mutex_);
    return items_.size();
  }

  /// Очищает окно: вычисление начинается заново.
  void reset() {
    const std::scoped_lock lock(mutex_);
    head_ = 0;
    count_ = 0;
  }

  /// Обрабатывает сэмплы `samples` по порядку: каждый сэмпл заменяется
  /// максимумом окна на нём на том же месте. Из окна уходят сэмплы с номером не
  /// больше `sample.index − capacity`, а из равных наибольших значений
  /// возвращается самое раннее. Номер каждого сэмпла больше номера
  /// последнего обработанного.
  void process(std::span<SampleValue> samples) {
    const std::scoped_lock lock(mutex_);
    for (SampleValue &sample : samples)
      sample = add(sample);
  }

private:
  /// Пустое окно на `capacity` сэмплов; `capacity` больше нуля.
  explicit SlidingMax(std::size_t capacity) : items_(capacity) {}

  /// Забирает окно `other`; его замок держит вызывающий.
  SlidingMax(SlidingMax &&other, const std::scoped_lock<std::mutex> & /*lock*/)
      : items_(other.items_), head_(std::exchange(other.head_, 0)),
        count_(std::exchange(other.count_, 0)) {}

  /// Добавляет сэмпл `sample` в окно и возвращает максимум окна. Зовётся под
  /// `mutex_`.
  SampleValue add(const SampleValue &sample) {
    while (count_ > 0 && items_[head_].index + items_.size() <= sample.index) {
      head_ = (head_ + 1) % items_.size();
      --count_;
    }
    while (count_ > 0 && back().value.value() < sample.value.value())
      --count_;
    items_[(head_ + count_) % items_.size()] = sample;
    ++count_;
    return items_[head_];
  }

  /// Последний добавленный сэмпл. Окно непусто. Зовётся под `mutex_`.
  const SampleValue &back() const { return items_[(head_ + count_ - 1) % items_.size()]; }

  // Окно — под этим замком.
  mutable std::mutex mutex_;
  std::vector<SampleValue> items_;
  std::size_t head_ = 0;
  std::size_t count_ = 0;
};

} // namespace training::core::detector
