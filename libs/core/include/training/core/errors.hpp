#pragma once

// Ошибки ядра (`training.core`).

namespace training::core {

/// Ошибка: переход в неразрешённое состояние — например, запуск того, что
/// уже запущено.
struct InvalidStep {
  /// Ошибки равны: у них нет данных.
  bool operator==(const InvalidStep &) const = default;
};

/// Ошибка: шаг ядра не запускается — например, детектор атак не создаётся на
/// частоте дискретизации транспорта.
struct CannotStartUnit {
  /// Ошибки равны: у них нет данных.
  bool operator==(const CannotStartUnit &) const = default;
};

} // namespace training::core
