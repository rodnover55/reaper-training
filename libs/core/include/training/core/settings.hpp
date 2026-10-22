#pragma once

// Настройки ядра тренажёра (`timing-display`): то, по чему ядро меряет ноты.

#include "training/core/hit_window.hpp"

#include <mutex>

namespace training::core {

/// Настройки ядра.
///
/// Многопоточность: все методы можно звать из любых потоков одновременно.
class Settings {
public:
  /// Создаёт настройки с окном попадания `window` и порогом тишины
  /// `silenceDb`, dBFS.
  Settings(const HitWindow &window, double silenceDb) noexcept
      : window_(window), silenceDb_(silenceDb) {}

  /// Копия настроек `other`, снятая под его замком.
  Settings(const Settings &other);

  /// Ставит значения настроек `other`, снятые под его замком.
  Settings &operator=(const Settings &other);

  /// Копия окна попадания.
  HitWindow window() const;

  /// Ставит окно попадания как есть: смещение и допуск не округляются.
  void setWindow(const HitWindow &window);

  /// Порог тишины, dBFS.
  double silenceDb() const;

  /// Ставит порог тишины `silenceDb` как есть.
  void setSilenceDb(double silenceDb);

  /// Правда, если совпадают все настройки.
  bool operator==(const Settings &other) const;

private:
  /// Копия настроек `other`; его замок держит вызывающий.
  Settings(const Settings &other, const std::scoped_lock<std::mutex> & /*lock*/)
      : window_(other.window_), silenceDb_(other.silenceDb_) {}

  // Значения настроек — под этим замком.
  mutable std::mutex mutex_;
  HitWindow window_;
  double silenceDb_;
};

} // namespace training::core
