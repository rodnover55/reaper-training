#pragma once

// Такты запуска в порядке прихода (`timing-display`, `hit-timing`).

#include "training/core/clock.hpp"
#include "training/core/training/state/bar.hpp"
#include "training/core/training/state/position.hpp"

#include <cstddef>
#include <mutex>
#include <optional>
#include <vector>

namespace training::core::training::state {

/// Такты запуска в порядке прихода. Такты могут пересекаться: доля такта
/// действует, если её время лежит в её такте (`Bar::contains`) и вне каждого
/// такта, пришедшего после него.
///
/// Созданный объект: тактов нет.
///
/// Многопоточность: все методы можно звать из любых потоков одновременно.
/// Такты отдаются копиями, снятыми под замком списка.
class Timeline {
public:
  /// Создаёт список без тактов.
  Timeline() = default;

  /// Копия списка `other`, снятая под его замком.
  Timeline(const Timeline &other);

  /// Ставит такты списка `other`, снятые под его замком.
  Timeline &operator=(const Timeline &other);

  /// Копия тактов в порядке прихода.
  std::vector<Bar> bars() const;

  /// Добавляет такт `bar` в конец `bars`. Прошлые такты, их доли и ноты
  /// остаются как есть.
  void add(const Bar &bar);

  /// Ставит ноту `note` в долю на позиции `position` после её прошлых нот.
  ///
  /// @return ложь, и такты остаются как есть, если такта `position.bar` нет,
  ///   у него нет доли `position.division` или доля не действует.
  bool addNote(Position position, const Note &note);

  /// Позиция действующей доли, ближайшей по времени к `time`, среди долей
  /// всех тактов. Из двух равно близких долей — ранняя по времени, в одном
  /// такте и в разных.
  ///
  /// Такты до и после времени нот добавляет вызывающий (`add`): нота чуть
  /// раньше первого такта лежит вне тактов.
  ///
  /// @return пусто, если `time` не лежит ни в одном такте или действующих
  ///   долей нет.
  std::optional<Position> nearest(Moment time) const;

  /// Время доли на позиции `position`.
  ///
  /// @return пусто, если такта `position.bar` нет, у него нет доли
  ///   `position.division` или доля не действует.
  std::optional<Moment> time(Position position) const;

private:
  /// Копия списка `other`; его замок держит вызывающий.
  Timeline(const Timeline &other, const std::scoped_lock<std::mutex> & /*lock*/)
      : bars_(other.bars_) {}

  /// Время доли на позиции `position`, как у `time`. Зовётся под `mutex_`.
  std::optional<Moment> timeOf(Position position) const;

  /// Правда, если доля такта с индексом `index` со временем `at` действует:
  /// `at` лежит в этом такте и вне каждого такта, пришедшего после него.
  /// Зовётся под `mutex_`.
  bool acts(std::size_t index, Moment at) const;

  // Такты — под этим замком.
  mutable std::mutex mutex_;
  std::vector<Bar> bars_;
};

} // namespace training::core::training::state
