#pragma once

// Журнал расширения: что расширение видит в REAPER и как быстро отвечает.
// Пишется в любой сборке (`platform-support`).

#include "input_settings.hpp"

#include "training/core/calibration/calibrator.hpp"
#include "training/core/clock.hpp"
#include "training/reaper/calibration.hpp"
#include "training/reaper/midi_event.hpp"

#include <fmt/format.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace training::reaper {

/// Наибольший размер журнала при открытии, байт: больший файл уходит в `.1`.
inline constexpr std::uintmax_t kJournalLimit = std::uintmax_t{5} * 1024 * 1024;

/// Открывает журнал в файле `path`, дописывая в конец. Если файл больше
/// `kJournalLimit`, он сначала переименовывается в `path` с `.1` в конце —
/// прежний такой файл пропадает, — и журнал начинается заново. Повторный
/// вызов при открытом журнале ничего не делает. Файл, который не открылся,
/// журнал молча не пишет. Зовётся при загрузке расширения из главного потока;
/// этот поток в журнале называется `main`.
///
/// @param path путь в UTF-8, как его отдаёт REAPER.
void openJournal(const std::string &path);

/// Закрывает журнал. Строки после закрытия никуда не пишутся.
void closeJournal();

/// Пишет строку с отметкой времени от открытия журнала и именем потока. Можно
/// звать из любого потока: строки не перемешиваются. Звуковому потоку журнал
/// не годится — запись в файл ждёт диск; оттуда отметки передаются очередью.
void journalLine(std::string_view line);

/// Время по часам журнала, в секундах от его открытия: для отметок, снятых
/// в одном потоке, а записанных в журнал из другого.
double journalSeconds(std::chrono::steady_clock::time_point at);

/// Момент открытия журнала по монотонным часам: от него журнал отсчитывает
/// отметки времени строк. Без блокировок — годится и звуковому потоку.
std::chrono::steady_clock::time_point journalOpenedAt();

/// Момент для журнала: секунды от точки отсчёта часов ядра с шестью знаками
/// после запятой — до микросекунд, меньшие доли отбрасываются, — например
/// «12.000345». Строки с моментами сравниваются как числа.
std::string momentText(core::Moment moment);

/// Выбранный вход `settings` по его виду входа для журнала: «channel 1» или
/// «MIDI «KeyStep 37» #3».
std::string inputText(const InputSettings &settings);

/// Нота события `event` для журнала: «note=60 channel=1 velocity=100»; канал —
/// с 1.
std::string noteText(const MidiEvent &event);

/// Форматирует строку по правилам {fmt} и пишет её в журнал (`journalLine`).
template <class... Args> void journal(fmt::format_string<Args...> format, Args &&...args) {
  journalLine(fmt::format(format, std::forward<Args>(args)...));
}

/// Приёмник строк журнала: получает строку за строкой, без перевода строки в
/// конце.
using JournalSink = std::function<void(std::string_view line)>;

/// Журнал хода калибровки: отдаёт каждое событие калибровки строкой журнала,
/// начиная её с «calibration: ». Уровни шума и атак — с одним знаком после
/// запятой.
///
/// Многопоточность: как у приёмника строк; порог до калибровки пишет и
/// читает один поток — тот, что зовёт `started` и `silenceSet`.
class CalibrationJournal final : public CalibrationEvents {
public:
  /// @param sink куда идут строки; по умолчанию — журнал расширения
  ///   (`journalLine`).
  explicit CalibrationJournal(JournalSink sink = journalLine);

  /// Пишет «started on input <канал с 1> (<имя>), silence <порог> dBFS» и
  /// запоминает порог до калибровки для `silenceSet`. Зовёт связка, когда
  /// начинает сессию калибровки.
  void started(int channel, const std::string &inputName, double silenceDb);

  /// Пишет «ended».
  void ended() override;

  /// Пишет «cancelled by playback».
  void cancelledByPlayback() override;

  /// Пишет «closed by playback».
  void closedByPlayback() override;

  /// Пишет «<частота> Hz, silence step».
  void silenceStep(double sampleRate) override;

  /// Пишет «sound during silence, silence step counts again».
  void soundDuringSilence() override;

  /// Пишет «noise <шум> dBFS, notes step».
  void notesStep(double noiseDb) override;

  /// Пишет «note <номер с 1> attack <уровень> dBFS».
  void attack(std::size_t note, double attackDb) override;

  /// Пишет «<вердикт>, noise <шум>, attacks <нот> from <тише всех> to <громче
  /// всех>, peak <пик>, threshold <порог> dBFS»; вердикт — «good», «noise too
  /// close», «clipping» или «not enough notes». Без нот тише и громче всех —
  /// `core::calibration::kFloorDb`.
  void finished(const core::calibration::Calibrator::State &state) override;

  /// Пишет «silence <порог до калибровки> -> <новый> dBFS»; порог до
  /// калибровки — из `started`.
  void silenceSet(double silenceDb) override;

  /// Пишет «applied anyway, silence <порог> dBFS».
  void appliedAnyway(double silenceDb) override;

  /// Пишет «closed by sound after the result».
  void closedBySound() override;

  /// Пишет «no sound from the audio device for <`kNoSoundTime`> s»: связка
  /// кончила подачу звука в калибровку, потому что звука не было
  /// `kNoSoundTime`.
  void noSound();

private:
  JournalSink sink_;
  double fromDb_ = 0.0;
};

} // namespace training::reaper
