#pragma once

// Строка состояния окна (`timing-display`, `input-calibration`): режим работы
// тренажёра, выбранный вход, компенсация задержки и скорость.

#include "training/core/clock.hpp"
#include "training/core/state/work_mode.hpp"

#include <string>
#include <string_view>

namespace training::render {

/// Выбранный вход, каким его видит хост.
enum class InputStatus {
  /// Вход есть, и хост отдаёт его звук или ноты.
  Present,
  /// Выбранного входа нет: канала у звуковой карты или устройства MIDI.
  NoInput,
  /// Выбранный вход MIDI выключен у хоста для записи на дорожки.
  NotEnabled,
};

/// Данные хоста для строки состояния окна: имя хоста, выбранный вход,
/// компенсация задержки и скорость.
struct StatusLine {
  /// Имя хоста, как его видит пользователь, — в тексте о выключенном входе MIDI.
  std::string_view host;

  /// Выбранный вход, каким его видит хост.
  InputStatus input = InputStatus::Present;

  /// Компенсация задержки выбранного входа, которая действует сейчас, в
  /// реальном времени: в запуске — снятая при его начале, без запуска — та,
  /// что будет действовать при запуске.
  core::Interval compensation;

  /// Скорость воспроизведения проекта.
  double rate = 1.0;

  /// Текст строки состояния: «`mode`, window `window`, latency 12.3 ms»,
  /// затем «, rate 0.75», если скорость отличается от 1, затем что делает
  /// тренажёр — « — selected input is not available» или « — MIDI input is
  /// not enabled in `host` preferences» по входу, а при входе, который есть,
  /// « — measuring» в режиме `core::state::WorkMode::Run` и « — waiting for
  /// playback» в остальных, — и в конце « · calibration cancelled: playback
  /// started», если калибровку прервал запуск.
  ///
  /// @param mode название режима сетки.
  /// @param window окно попадания диапазоном.
  /// @param workMode режим работы тренажёра.
  /// @param calibrationCancelled прервал ли калибровку запуск транспорта.
  std::string text(std::string_view mode, std::string_view window,
                   core::state::WorkMode workMode, bool calibrationCancelled) const;

  /// Правда, если у сводок совпадают все поля.
  friend bool operator==(const StatusLine &, const StatusLine &) = default;
};

} // namespace training::render
