#pragma once

// Запуски транспорта и строки тактов последнего запуска (`timing-display`).

#include "training/trainer/compensation.hpp"

#include "training/grid/bars.hpp"
#include "training/grid/hit_window.hpp"
#include "training/grid/mode.hpp"
#include "training/grid/timeline.hpp"

namespace training::trainer {

/// Запуски транспорта и строки тактов. Запуск — воспроизведение или запись от
/// начала или продолжения после паузы до остановки или паузы. Строки тактов
/// живут до следующего запуска: остановка их оставляет, новый запуск очищает.
///
/// Созданный объект: запуска нет, строк нет, режим «четверти», компенсация
/// нулевая.
///
/// Многопоточность: один поток вызовов.
class Transport {
public:
  /// Начинает запуск: запоминает момент начала и компенсацию, очищает строки
  /// тактов. Если запуск уже идёт, ничего не делает: хост может сообщить то же
  /// состояние транспорта ещё раз.
  ///
  /// @param now момент начала, секунды монотонных часов.
  /// @param compensation компенсация задержки входов в момент начала: держится
  ///   до конца запуска, как постоянный сдвиг у записи.
  /// @return правда, если запуск начался; ложь, если он уже шёл.
  bool start(double now, const Compensation &compensation);

  /// Кончает запуск: текущего удара в строках тактов больше нет, строки
  /// остаются. Без запуска ничего не делает.
  ///
  /// @return правда, если запуск кончился; ложь, если его не было.
  bool stop();

  /// Правда, если идёт запуск.
  bool running() const { return running_; }

  /// Компенсация задержки, снятая при начале последнего запуска; до первого
  /// запуска — нулевая.
  const Compensation &compensation() const { return compensation_; }

  /// Решает, ставить ли в строки тактов атаку, найденную в звуке входа.
  ///
  /// @param blockMoment момент, когда блок звука с атакой пришёл от звуковой
  ///   карты, секунды тех же монотонных часов, что `now` у `start`.
  /// @return правда, если запуск идёт, блок пришёл не раньше начала запуска и
  ///   частота звука, по которой снята компенсация, известна. Атака из блока,
  ///   пришедшего до начала запуска, — прошлого запуска, даже если звук шёл
  ///   без перерыва.
  bool takesOnset(double blockMoment) const;

  /// Строки тактов последнего запуска.
  const grid::Bars &bars() const { return bars_; }

  /// Задаёт режим для ударов, которые ещё не начались (`grid::Bars::setMode`).
  void setMode(grid::Mode mode) { bars_.setMode(mode); }

  /// Сообщает строкам тактов позицию воспроизведения, которую сейчас слышно
  /// (`grid::Bars::play`).
  ///
  /// @return правда, если сверху встала строка другого такта или нового
  ///   прохода.
  bool play(double beats, double loopBeats, const grid::HitWindow &window,
            const grid::Timeline &timeline) {
    return bars_.play(beats, loopBeats, window, timeline);
  }

  /// Ставит ноту в строки тактов (`grid::Bars::addNote`).
  void addNote(double time, double rate, const grid::HitWindow &window,
               const grid::Timeline &timeline) {
    bars_.addNote(time, rate, window, timeline);
  }

private:
  grid::Bars bars_;
  Compensation compensation_;
  double startedAt_ = 0.0;
  bool running_ = false;
};

} // namespace training::trainer
