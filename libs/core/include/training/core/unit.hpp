#pragma once

// Запущенный шаг ядра (`training.core.Unit`): обработчик, которому вызывающий
// передаёт данные и у которого берёт результат.

namespace training::core {

/// Запущенный шаг: принимает данные через `Callback` и отдаёт результат
/// `result`. `Callback` — интерфейс приёма шага: калибровки
/// (`calibration::Callback`) или тренировки (`training::Callback`).
///
/// Многопоточность: методы реализации можно звать из любых потоков
/// одновременно.
template <class Callback, class T> class Unit : public Callback {
public:
  /// Результат шага: копия, снятая под замком шага, с его состоянием запуска.
  /// Копия только для чтения: шаг остаётся как есть.
  // Чистая виртуальная функция не инстанцируется ни одним компилятором, а
  // чек считает её такой же, как функцию с телом.
  // NOLINTNEXTLINE(portability-template-virtual-member-function)
  virtual T result() const = 0;
};

} // namespace training::core
