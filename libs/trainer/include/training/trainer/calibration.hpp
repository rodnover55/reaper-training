#pragma once

// Сессия калибровки входа: от запуска калибровки до её конца (`input-calibration`).

#include "training/trainer/clock.hpp"
#include "training/trainer/journal.hpp"
#include "training/trainer/measurement.hpp"

#include "training/onset/calibration.hpp"

#include <chrono>
#include <optional>
#include <span>
#include <string>

namespace training::trainer {

/// Сколько времени без звука от звуковой карты обрывает калибровку.
inline constexpr std::chrono::nanoseconds kNoSoundTime = std::chrono::seconds(2);

/// Сессия калибровки входа.
struct CalibrationSession {
  /// Ход калибровки; пусто, пока не пришёл первый звук: до него неизвестна
  /// частота дискретизации.
  std::optional<onset::Calibration> calibration;

  /// Порог тишины до калибровки, dBFS.
  double previousSilenceDb = 0.0;

  /// Имя калибруемого входного канала звуковой карты у хоста.
  std::string channelName;

  /// Правда, если звук от звуковой карты не приходил `kNoSoundTime` и
  /// калибровка оборвана без итога.
  bool noSound = false;

  /// Правда, если калибровка подвела итог (`onset::Calibration::result`).
  bool finished = false;
};

/// Этап сессии калибровки.
enum class CalibrationPhase {
  /// Шаг тишины, в том числе до первого звука.
  Silence,
  /// Шаг нот.
  Notes,
  /// Звук от звуковой карты не приходит: калибровка оборвана без итога.
  NoSound,
  /// Итог калибровки подведён.
  Result,
};

/// Этап сессии калибровки `session`.
CalibrationPhase phaseOf(const CalibrationSession &session);

/// Что связке сделать после хода калибровки.
struct CalibrationUpdate {
  /// Правда, если сессия изменилась так, что окно пора перерисовать.
  bool changed = false;

  /// Порог тишины, который калибровка поставила, dBFS: его надо поставить в
  /// настройки и сохранить. Пусто — порог не меняется.
  std::optional<double> silenceDb;
};

/// Калибровка входа: сессии калибровки от запуска до конца и то, прервал ли
/// калибровку запуск транспорта.
///
/// Сессия кончается Finish, Close, Cancel, «Apply anyway», закрытием окна
/// (`end`), сменой входа (`onInputChange`), запуском транспорта (`onStart`) и
/// ударом по струнам после итога (`update`).
///
/// Созданный объект: сессии нет, калибровку запуск не прерывал.
///
/// Многопоточность: один поток вызовов.
class Calibrator {
public:
  /// @param journal куда писать ход калибровки.
  explicit Calibrator(Journal journal = {});

  /// Начинает калибровку выбранного канала звуковой карты; прежняя сессия
  /// кончается без изменений (`end`). Отметка прерванной запуском
  /// калибровки снимается.
  ///
  /// @param running идёт ли запуск транспорта.
  /// @param measurement состояние измерения: выбранный вход и есть ли он.
  /// @param channel выбранный канал, 0 — первый.
  /// @param silenceDb порог тишины до калибровки, dBFS.
  /// @param now момент начала: от него отсчитывается `kNoSoundTime` до первого
  ///   звука.
  /// @return ложь, если идёт запуск, выбран вход MIDI или выбранного канала
  ///   нет: калибровка не начата, прежняя сессия осталась.
  bool start(bool running, const Measurement &measurement, int channel, double silenceDb,
             Moment now);

  /// Кончает сессию без изменений порога тишины; поставленный удачной
  /// калибровкой порог остаётся. Без сессии ничего не делает.
  void end();

  /// Отмечает смену входа: сессия кончается без изменений порога тишины, как
  /// `end`, — она меряла прежний вход. Без сессии ничего не делает. Отметку
  /// прерванной запуском калибровки не трогает.
  void onInputChange();

  /// Отмечает начало запуска транспорта: оно кончает сессию. Шаги калибровки
  /// прерываются — до конца запуска это видно в `cancelledByPlayback`, — а
  /// кончившаяся калибровка просто закрывается. Без сессии ничего не делает.
  void onStart();

  /// Отмечает конец запуска транспорта: снимает отметку прерванной запуском
  /// калибровки.
  void onStop() { cancelledByPlayback_ = false; }

  /// Подаёт следующий кусок звука выбранного канала, снятого при
  /// остановленном транспорте. Без сессии и после обрыва без звука ничего не
  /// делает. Первый кусок и кусок другой частоты начинают калибровку заново,
  /// с шага тишины; после итога звук другой частоты не слушается.
  ///
  /// @param samples сэмплы, доли полной шкалы.
  /// @param sampleRate частота дискретизации, Гц; больше 2000.
  /// @param gap кусок не продолжает прошлый: звук пропадал.
  void feed(std::span<const float> samples, double sampleRate, bool gap);

  /// Подводит ход сессии на момент `now` после кусков, поданных с прошлого
  /// вызова.
  ///
  /// - Калибровка подводит итог; удачный итог ставит порог тишины.
  /// - Если звука не было `kNoSoundTime` от начала сессии или от
  ///   последнего куска, сессия обрывается без итога (`noSound`).
  /// - Удар по струнам после итога (`onset::Calibration::soundAfterResult`)
  ///   кончает сессию.
  ///
  /// @param now момент хода.
  CalibrationUpdate update(Moment now);

  /// «Apply anyway»: ставит порог тишины, посчитанный калибровкой, хотя она
  /// его не поставила, и кончает сессию.
  ///
  /// @return порог тишины, dBFS, который надо поставить в настройки и
  ///   сохранить; пусто — итога нет или в нём нет ни одной ноты, и ничего не
  ///   меняется.
  std::optional<double> applyAnyway();

  /// Сессия калибровки; пусто, если её нет.
  const std::optional<CalibrationSession> &session() const { return session_; }

  /// Правда, если сессия есть и калибровка кончилась: подвела итог или
  /// оборвалась без звука.
  bool finished() const { return session_ && (session_->finished || session_->noSound); }

  /// Порог тишины, который поставит «Apply anyway» (`applyAnyway`), dBFS,
  /// прижатый к границам настройки. Есть, если калибровка подвела итог по
  /// всем нотам, но порог не поставила: шум слишком близко к атаке или вход
  /// перегружен. Иначе пусто.
  std::optional<double> anywaySilenceDb() const;

  /// Правда, если сессия есть и ждёт звук входа: звук при остановленном
  /// транспорте надо подавать в `feed`.
  bool listening() const { return session_ && !session_->noSound; }

  /// Правда, если калибровку прервал запуск транспорта и он ещё идёт.
  bool cancelledByPlayback() const { return cancelledByPlayback_; }

private:
  CalibrationUpdate finish(CalibrationSession &session,
                           const onset::CalibrationResult &result);

  Journal journal_;
  std::optional<CalibrationSession> session_;
  // Когда пришёл последний звук и был ли звук с прошлого update.
  Moment lastSound_{};
  bool heard_ = false;
  bool cancelledByPlayback_ = false;
};

} // namespace training::trainer
