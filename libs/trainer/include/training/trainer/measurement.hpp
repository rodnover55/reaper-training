#pragma once

// Состояние измерения тренажёра: что он сейчас делает, каким входом и с какой
// компенсацией задержки (`timing-display`, `input-calibration`).

#include "training/trainer/compensation.hpp"
#include "training/trainer/settings.hpp"

#include <optional>
#include <string>

namespace training::trainer {

/// Что сейчас делает измерение.
enum class Measuring {
  /// Транспорт остановлен или на паузе.
  Waiting,
  /// Воспроизведение или запись: ноты меряются.
  Running,
  /// Выбранного входа нет: канала у звуковой карты или устройства MIDI.
  NoInput,
  /// Выбранный вход MIDI не включён у хоста для записи на дорожки.
  NotEnabled,
};

/// Выбранный вход, каким его видит хост.
struct InputState {
  /// Имя выбранного входного канала звуковой карты у хоста; пусто — у
  /// звуковой карты нет такого канала или выбран вход MIDI.
  std::string channelName;

  /// Есть ли выбранный канал у звуковой карты по последнему блоку звука, в
  /// котором это проверялось; пусто — после выбора канала не проверялось.
  std::optional<bool> channelPresent;

  /// Имя выбранного входа MIDI у хоста, с псевдонимом; пусто — вход MIDI не
  /// выбран или устройства с его именем сейчас нет.
  std::optional<std::string> midiName;

  /// Включён ли найденный вход MIDI у хоста для записи на дорожки.
  bool midiEnabled = false;
};

/// Состояние измерения тренажёра.
struct Measurement {
  /// Что делает измерение.
  Measuring measuring = Measuring::Waiting;

  /// Имя выбранного входа: входного канала звуковой карты или устройства
  /// MIDI с псевдонимом; пусто — входа нет.
  std::string inputName;

  /// Правда, если выбран вход MIDI.
  bool midi = false;

  /// Компенсация задержки выбранного входа, которая действует сейчас, мс
  /// реального времени: в запуске — снятая при его начале, без запуска — та,
  /// что будет действовать при запуске.
  double compensationMs = 0.0;

  /// Скорость воспроизведения проекта.
  double rate = 1.0;

  /// Правда, если калибровку входа прервал запуск транспорта; держится, пока
  /// транспорт не остановится.
  bool calibrationCancelled = false;

  friend bool operator==(const Measurement &, const Measurement &) = default;
};

/// Правда, если тренажёр меряет входом MIDI: вход MIDI выбран, есть, включён у
/// хоста для записи на дорожки, и идёт запуск транспорта.
inline bool measuresMidi(const Measurement &measurement) {
  return measurement.midi && measurement.measuring == Measuring::Running;
}

/// Состояние измерения тренажёра.
///
/// Вход звуковой карты есть, если у хоста есть имя выбранного канала и
/// последний блок звука, где канал проверялся, его не опроверг. Вход MIDI
/// есть, если устройство с его именем найдено; невключённый у хоста вход MIDI
/// — `Measuring::NotEnabled`. При входе, который есть, — `Running`, если идёт
/// запуск, иначе `Waiting`.
///
/// @param settings настройки: выбранный вход.
/// @param input выбранный вход, каким его видит хост.
/// @param running идёт ли запуск транспорта.
/// @param atStart компенсация, снятая при начале запуска: действует, пока
///   запуск идёт.
/// @param current компенсация сейчас: без запуска — та, что будет действовать
///   при запуске.
/// @param rate скорость воспроизведения проекта.
/// @param calibrationCancelled прервал ли запуск калибровку.
Measurement measurementOf(const Settings &settings, const InputState &input, bool running,
                          const Compensation &atStart, const Compensation &current,
                          double rate, bool calibrationCancelled);

} // namespace training::trainer
