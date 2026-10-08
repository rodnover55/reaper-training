#pragma once

// Запуски транспорта, поиск атак в звуке запуска, отбор нот MIDI и строки
// тактов последнего запуска (`timing-display`).

#include "training/trainer/clock.hpp"
#include "training/trainer/compensation.hpp"
#include "training/trainer/journal.hpp"
#include "training/trainer/midi_notes.hpp"
#include "training/trainer/onset_search.hpp"

#include "training/grid/bars.hpp"
#include "training/grid/hit_window.hpp"
#include "training/grid/mode.hpp"
#include "training/grid/timeline.hpp"

namespace training::trainer {

/// Запуски транспорта и строки тактов. Запуск — воспроизведение или запись от
/// начала или продолжения после паузы до остановки или паузы. Строки тактов
/// живут до следующего запуска: остановка их оставляет, новый запуск очищает.
///
/// Запуск начинают и кончают только `start` и `stop`, вместе с ним — поиск
/// атак в звуке входа (`onsetSearch`) и отбор нот MIDI (`midiNotes`): поиск
/// разбирает звук, взятый не раньше начала идущего запуска, а отбор
/// отсчитывает count-in от позиции начала.
///
/// Созданный объект: запуска нет, строк нет, режим «четверти», компенсация
/// нулевая.
///
/// Многопоточность: методы — из одного потока, а поиск атак (`onsetSearch`) —
/// из одного другого; общее у потоков только состояние запуска.
class Transport {
public:
  /// @param journal куда писать найденные атаки, атаки и ноты MIDI, которые
  ///   не измерены, и когда сдвинулась позиция.
  explicit Transport(Journal journal = {});

  /// Начинает запуск: запоминает момент начала и компенсацию, очищает строки
  /// тактов, начинает отбор нот MIDI (`MidiNotes::start`). Поиск атак перед
  /// следующим блоком забывает прошлый звук и берёт блоки, взятые не раньше
  /// `now`. Если запуск уже идёт, ничего не делает: хост может сообщить то же
  /// состояние транспорта ещё раз.
  ///
  /// @param now момент начала.
  /// @param position позиция обработки в момент начала, с: count-in нот MIDI
  ///   длится, пока позиция не ушла от неё.
  /// @param compensation компенсация задержки входов в момент начала: держится
  ///   до конца запуска, как постоянный сдвиг у записи.
  /// @return правда, если запуск начался; ложь, если он уже шёл.
  bool start(Moment now, double position, const Compensation &compensation);

  /// Кончает запуск: текущего удара в строках тактов больше нет, строки
  /// остаются. Поиск атак перед следующим блоком забывает прошлый звук и
  /// блоки не разбирает. Без запуска ничего не делает.
  ///
  /// @return правда, если запуск кончился; ложь, если его не было.
  bool stop();

  /// Правда, если идёт запуск.
  bool running() const { return run_.load() != kNoRun; }

  /// Компенсация задержки, снятая при начале последнего запуска; до первого
  /// запуска — нулевая.
  const Compensation &compensation() const { return compensation_; }

  /// Решает, ставить ли в строки тактов атаку, найденную в звуке входа.
  ///
  /// @param blockMoment момент, когда блок звука с атакой пришёл от звуковой
  ///   карты.
  /// @return правда, если запуск идёт, блок пришёл не раньше начала запуска и
  ///   частота звука, по которой снята компенсация, известна. Атака из блока,
  ///   пришедшего до начала запуска, — прошлого запуска, даже если звук шёл
  ///   без перерыва.
  bool takesOnset(Moment blockMoment) const;

  /// Поиск атак в звуке входа запусков. Блоки в него подаются из одного
  /// потока; запуск у него — запуск транспорта.
  OnsetSearch &onsetSearch() { return search_; }

  /// Отбор нот MIDI запусков: какие события истории MIDI-входа измерять.
  MidiNotes &midiNotes() { return midiNotes_; }

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
  RunState run_{kNoRun};
  // После состояния запуска: поиск держит ссылку на него.
  OnsetSearch search_;
  MidiNotes midiNotes_;
};

} // namespace training::trainer
