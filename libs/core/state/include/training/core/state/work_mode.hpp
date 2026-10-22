#pragma once

// Режим работы тренажёра: ожидание, запуск или калибровка.

namespace training::core::state {

/// Режим работы тренажёра.
enum class WorkMode {
  /// Ожидание: запуск ещё не начинался или кончился.
  Idle,
  /// Запуск: между началом и концом запуска.
  Run,
  /// Калибровка: сессия калибровки ждёт звук входа.
  Calibration,
};

} // namespace training::core::state
