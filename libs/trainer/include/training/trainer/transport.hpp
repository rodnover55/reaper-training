#pragma once

// Запуски транспорта, поиск атак в звуке запуска, отбор нот MIDI и строки
// тактов последнего запуска (`timing-display`).

#include "training/trainer/clock.hpp"
#include "training/trainer/compensation.hpp"
#include "training/trainer/count_in.hpp"
#include "training/trainer/journal.hpp"
#include "training/trainer/midi_notes.hpp"
#include "training/trainer/onset_search.hpp"

#include "training/grid/bars.hpp"
#include "training/grid/hit_window.hpp"
#include "training/grid/mode.hpp"
#include "training/grid/timeline.hpp"

#include <cstdint>
#include <optional>

namespace training::trainer {

/// Запуски транспорта и строки тактов. Запуск — воспроизведение или запись от
/// начала или продолжения после паузы до остановки или паузы. Строки тактов
/// живут до следующего запуска: остановка их оставляет, новый запуск очищает.
///
/// Запуск начинают и кончают только `start` и `stop`, вместе с ним — поиск
/// атак в звуке входа (`onsetSearch`) и отбор нот MIDI (`midiNotes`): оба
/// отдают ноты идущему запуску и кладут ноты отсчёта и ноты, сыгранные до
/// Play, перед позицией начала.
///
/// Созданный объект: запуска нет, строк нет, режим «четверти», компенсация
/// нулевая, отсчёта нет.
///
/// Многопоточность: методы — из одного потока, а поиск атак (`onsetSearch`) —
/// из одного другого; общее у потоков только состояние запуска и его начало и
/// отсчёт.
class Transport {
public:
  /// @param journal куда писать найденные атаки и ноты MIDI, которые не
  ///   измерены.
  explicit Transport(Journal journal = {});

  /// Начинает запуск: запоминает момент начала, компенсацию и отсчёт, очищает
  /// строки тактов, начинает отбор нот MIDI (`MidiNotes::start`). Для поиска
  /// атак блоки запуска — взятые не раньше `now`; первый из них начинает
  /// отсчёт. Если запуск уже идёт, ничего не делает: хост может сообщить то же
  /// состояние транспорта ещё раз.
  ///
  /// @param now момент начала.
  /// @param position позиция обработки в момент начала, с: от неё ноты отсчёта
  ///   лежат на шкале назад.
  /// @param compensation компенсация задержки входов в момент начала: держится
  ///   до конца запуска, как постоянный сдвиг у записи.
  /// @param countIn отсчёт перед запуском; по умолчанию — без отсчёта.
  /// @param rate скорость воспроизведения проекта в момент начала; больше
  ///   нуля.
  /// @param leadInEnd такт, которым кончается затакт — пре-ролл или отсчёт:
  ///   строки тактов до него подписываются от него назад
  ///   (`grid::Bars::setLeadIn`); пусто — затакта нет.
  /// @return правда, если запуск начался; ложь, если он уже шёл.
  bool start(Moment now, double position, const Compensation &compensation,
             const CountIn &countIn = {}, double rate = 1.0,
             std::optional<std::int64_t> leadInEnd = std::nullopt);

  /// Кончает запуск: текущего удара в строках тактов больше нет, строки
  /// остаются. Поиск атак слушает вход дальше, но атак не отдаёт до
  /// следующего запуска. Без запуска ничего не делает.
  ///
  /// @return правда, если запуск кончился; ложь, если его не было.
  bool stop();

  /// Правда, если идёт запуск.
  bool running() const { return run_.load() != kNoRun; }

  /// Момент начала идущего запуска; `kNoRun` — запуска нет.
  Moment started() const { return run_.load(); }

  /// Компенсация задержки, снятая при начале последнего запуска; до первого
  /// запуска — нулевая.
  const Compensation &compensation() const { return compensation_; }

  /// Отсчёт перед последним запуском; до первого запуска — без отсчёта.
  const CountIn &countIn() const { return countIn_; }

  /// Решает, ставить ли в строки тактов атаку, найденную в звуке входа.
  ///
  /// @param onsetRun момент начала запуска, которому поиск атак отдал атаку
  ///   (`AudioOnset::run`).
  /// @return правда, если идёт этот запуск и частота звука, по которой снята
  ///   компенсация, известна. Атаку, отданную прошлому запуску, но забранную
  ///   после его конца, строки не берут.
  bool takesOnset(Moment onsetRun) const;

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
  /// (`grid::Bars::play`). Пока она не дошла до начала отсчёта по тактам —
  /// позиции начала минус длина отсчёта по тактам (`CountIn::barsSeconds`), без
  /// отсчёта — позиции начала, — она считается этим началом: хост начинает
  /// отсчёт раньше, чем по тактам, на остаток до целого блока звука, и строки
  /// такта, в который попал только этот остаток, у запуска нет. Позиция,
  /// ушедшая раньше начала потом — по петле или перемотке, — остаётся как
  /// есть.
  ///
  /// @return правда, если сверху встала строка другого такта или нового
  ///   прохода.
  bool play(double beats, double loopBeats, const grid::HitWindow &window,
            const grid::Timeline &timeline);

  /// Ставит ноту в строки тактов (`grid::Bars::addNote`). Ноту, сыгранную
  /// раньше позиции начала, берёт только узел не раньше начала запуска на
  /// шкале — позиции начала минус длина отсчёта: нота до Play — false start
  /// первого узла запуска, а узлы до запуска нот не берут.
  ///
  /// @param beforeStart правда, если место ноты раньше позиции начала: нота
  ///   отсчёта или нота, сыгранная до Play.
  /// @return правда, если нота легла в узел.
  bool addNote(double time, double rate, const grid::HitWindow &window,
               const grid::Timeline &timeline, bool beforeStart = false) {
    return bars_.addNote(time, rate, window, timeline,
                         beforeStart ? std::optional(runStart_) : std::nullopt);
  }

private:
  grid::Bars bars_;
  Compensation compensation_;
  CountIn countIn_;
  // Начало запуска на шкале: позиция начала минус длина отсчёта, с.
  double runStart_ = 0.0;
  // Начало отсчёта по тактам на шкале: позиция начала минус длина отсчёта по
  // тактам, с.
  double barsStart_ = 0.0;
  // Слышимая позиция запуска дошла до начала отсчёта по тактам; без запуска —
  // правда.
  bool reachedBarsStart_ = true;
  RunState run_{kNoRun};
  RunLeadIn leadIn_;
  // После состояния запуска и отсчёта: поиск держит ссылки на них.
  OnsetSearch search_;
  MidiNotes midiNotes_;
};

} // namespace training::trainer
