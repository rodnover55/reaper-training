#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace training::onset {

/// Наибольшее значение в окне последних сэмплов потока — монотонная очередь в
/// кольце фиксированной длины. Добавление и выход из окна стоят в среднем
/// O(1), память выделяется один раз, в конструкторе.
///
/// `Item` — значение с номером сэмпла: поля `index` (`std::uint64_t`) и
/// `value` (`double`), остальные поля переносятся как есть.
///
/// В очереди лежат кандидаты в максимум: номера возрастают, значения убывают,
/// голова — максимум окна. Значение, за которым пришло большее или равное,
/// максимумом уже не станет и уходит сразу.
template <class Item> class SlidingMax {
public:
  /// `capacity` — сколько значений с разными номерами бывает в окне; больше
  /// нуля.
  explicit SlidingMax(std::size_t capacity) : items_(capacity) {}

  /// Очищает окно.
  void clear() {
    head_ = 0;
    count_ = 0;
  }

  bool empty() const { return count_ == 0; }

  /// Добавляет значение. Номер не меньше номеров в окне; значение с тем же
  /// номером, что у последнего, заменяет его, если не меньше. Перед
  /// добавлением всё старше окна должно быть убрано `dropBefore`.
  void push(const Item &item) {
    while (count_ > 0 && back().value <= item.value)
      --count_;
    items_[(head_ + count_) % items_.size()] = item;
    ++count_;
  }

  /// Убирает из окна значения с номером меньше `index`.
  void dropBefore(std::uint64_t index) {
    while (count_ > 0 && items_[head_].index < index) {
      head_ = (head_ + 1) % items_.size();
      --count_;
    }
  }

  /// Наибольшее значение окна, а при равных — самое позднее. Окно не пусто.
  const Item &max() const { return items_[head_]; }

private:
  const Item &back() const { return items_[(head_ + count_ - 1) % items_.size()]; }

  std::vector<Item> items_;
  std::size_t head_ = 0;
  std::size_t count_ = 0;
};

} // namespace training::onset
