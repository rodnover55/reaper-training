#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace training::grid {

/// Размер такта.
struct Meter {
  /// Ударов в такте — числитель размера; больше нуля.
  int beats = 4;

  /// Длительность удара — знаменатель размера: 4 — четверть, 8 — восьмая.
  int unit = 4;

  friend bool operator==(const Meter &, const Meter &) = default;
};

/// Смена темпа или размера проекта в такте.
struct Change {
  /// Место смены в ударах от начала такта: 0 — первый удар, 2.5 — середина
  /// третьего.
  double beat = 0.0;

  /// Размер до смены; пусто — размер здесь не меняется.
  std::optional<Meter> meterFrom;

  /// Размер после смены; есть вместе с `meterFrom`.
  std::optional<Meter> meterTo;

  /// Темп до смены, четвертей в минуту, как у хоста; у плавного перехода —
  /// темп в его начале. Пусто — темп здесь не меняется.
  std::optional<double> tempoFrom;

  /// Темп после смены, четвертей в минуту; у плавного перехода — темп в его
  /// конце. Есть вместе с `tempoFrom`.
  std::optional<double> tempoTo;
};

/// Такт проекта.
struct Bar {
  /// Такт от начала проекта: 0 — первый, его номер для показа — 1.
  std::int64_t index = 0;

  /// Первый удар такта от начала проекта.
  std::int64_t firstBeat = 0;

  /// Размер такта: число ударов и их длительность.
  Meter meter;

  /// Смены темпа и размера в такте по возрастанию места.
  std::vector<Change> changes;
};

/// Карта темпа проекта: перевод между временем шкалы и ударами метронома и
/// такты проекта. Удар — единица знаменателя размера, как у метронома хоста
/// (findings.md, R3).
///
/// Реализация в модуле расширения спрашивает карту темпа хоста, в тестах —
/// считает по заданным темпам и размерам.
class Timeline {
public:
  virtual ~Timeline() = default;

  Timeline() = default;
  Timeline(const Timeline &) = default;
  Timeline &operator=(const Timeline &) = default;
  Timeline(Timeline &&) = default;
  Timeline &operator=(Timeline &&) = default;

  /// Удары от начала проекта, дробные, в момент `time` шкалы, с.
  virtual double beatsAt(double time) const = 0;

  /// Время шкалы, с, через `beats` ударов от начала проекта.
  virtual double timeAt(double beats) const = 0;

  /// Такт, в котором лежит удар номер `beat` от начала проекта (0 — первый
  /// удар проекта): его место, размер и смены темпа и размера в нём.
  virtual Bar barOf(std::int64_t beat) const = 0;
};

} // namespace training::grid
