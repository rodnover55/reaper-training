#pragma once

#include <cstdint>

namespace training::grid {

/// Такт и удар в такте, оба считаются с 1.
struct BarBeat {
  int bar = 1;
  int beat = 1;
};

/// Карта темпа проекта: перевод между временем шкалы и ударами метронома.
/// Удар — единица знаменателя размера, как у метронома хоста (findings.md,
/// R3).
///
/// Реализация в модуле расширения спрашивает карту темпа хоста, в тестах —
/// считает по заданному темпу.
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

  /// Такт и удар в такте для удара номер `beat` от начала проекта (0 —
  /// первый удар проекта).
  virtual BarBeat barBeat(std::int64_t beat) const = 0;
};

} // namespace training::grid
