#pragma once

#include "training/core/calibration/callback.hpp"
#include "training/core/calibration/notes_step.hpp"
#include "training/core/calibration/result_step.hpp"
#include "training/core/calibration/silence_step.hpp"
#include "training/core/calibration/step.hpp"
#include "training/core/clock.hpp"
#include "training/core/detector/detector.hpp"
#include "training/core/errors.hpp"
#include "training/core/input/sound_receiver.hpp"
#include "training/core/types.hpp"
#include "training/core/unit.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace training::core::calibration {

/// Данные калибровки со всех шагов — копия на момент `result`. Значение,
/// которое не используется на текущем шаге, не актуально.
struct State {
  /// На каком шаге вычислений находится калибратор.
  Step step = Step::Off;

  /// Уровень шума входящего сигнала, dBFS: во время `Step::Silence` —
  /// `SilenceStep::State::noiseDb`, во время `Step::Notes` и `Step::Done` —
  /// финальное значение шага тишины.
  double noiseDb = kFloorDb;

  /// Уровень текущего сигнала, dBFS: во время `Step::Silence` —
  /// `SilenceStep::State::levelDb`, во время `Step::Notes` —
  /// `NotesStep::State::levelDb`.
  double levelDb = kFloorDb;

  /// Время с последнего звука во время `Step::Silence`
  /// (`SilenceStep::State::sinceSound`).
  Interval sinceSound;

  /// Время до конца шага тишины во время `Step::Silence`
  /// (`SilenceStep::State::silenceLeft`).
  Interval silenceLeft;

  /// Уровни атаки найденных нот, dBFS (`NotesStep::State::attacksDb`):
  /// обновляются в `Step::Notes`, в `Step::Done` сохраняют последнее
  /// значение.
  std::vector<double> attacksDb;

  /// Время с последней ноты во время `Step::Notes`
  /// (`NotesStep::State::sinceLastNote`).
  Interval sinceLastNote;

  /// Пик сигнала, dBFS (`NotesStep::State::peakDb`): обновляется в
  /// `Step::Notes`, в `Step::Done` сохраняет последнее значение.
  double peakDb = kFloorDb;

  /// Итог измерения (`NotesStep::State::verdict`) во время `Step::Done`.
  NotesStep::Verdict verdict = NotesStep::Verdict::NotEnoughNotes;

  /// Вычисленный порог тишины, dBFS (`NotesStep::State::thresholdDb`), во
  /// время `Step::Done`.
  double thresholdDb = kFloorDb;
};

/// Калибровка входа: по звуку одного канала меряет шум (`SilenceStep`),
/// уровни атаки нот и пик сигнала (`NotesStep`), подводит итог и ждёт звука,
/// чтобы остановиться (`ResultStep`).
///
/// Блоки, которые приходят в калибратор, он стыкует по времени: начало блока,
/// которое лежит в уже полученном звуке, отбрасывается, а пропущенный между
/// блоками звук идёт в шаги тишиной. Блок идёт текущему шагу целиком;
/// следующий шаг начинает со следующего блока.
///
/// Ноты ищет только шаг нот своим детектором (`NotesStep::start`).
///
/// Созданный объект: калибратор выключен (`Step::Off`).
///
/// Многопоточность: все методы можно звать из любых потоков одновременно.
class Calibrator final : public Unit<Callback, State> {
private:
  /// Ключ конструктора: создать калибратор может только `create`.
  struct Key {
    /// Создаёт ключ; явный: снаружи ключ не создаётся записью `{}`.
    explicit Key() = default;
  };

public:
  /// Данные калибровки со всех шагов (`calibration::State`).
  using State = calibration::State;

  /// Создаёт калибратор блоков частоты `sampleRate` и его детектор атак на
  /// этой частоте: шаги тишины и итога берут у детектора фильтры, а шаг нот —
  /// настройки своего детектора.
  ///
  /// @return `CannotStartUnit`, если детектор атак на частоте `sampleRate`
  ///   не создаётся (`detector::Detector::create`).
  static std::expected<std::unique_ptr<Calibrator>, CannotStartUnit>
  create(SampleRate sampleRate);

  /// Калибратор блоков частоты детектора `detector`
  /// (`detector::Detector::settings`), который забирает детектор. Зовёт
  /// только `create`.
  Calibrator(Key /*key*/, detector::Detector detector);

  /// Запускает калибратор: переводит `Step::Off` → `Step::Silence`.
  ///
  /// @return `InvalidStep`, если калибратор не в `Step::Off`.
  std::expected<void, InvalidStep> start();

  /// Останавливает калибратор: переводит `Step::Silence`, `Step::Notes` и
  /// `Step::Done` → `Step::Off` и выключает включённые шаги.
  void stop();

  /// Состояние текущего шага выполнения.
  State result() const override;

  /// Частота дискретизации блоков калибратора — частота его детектора.
  SampleRate sampleRate() const override { return rate_; }

  /// Передаёт блок шагу по состоянию калибратора: `Step::Silence` — шагу
  /// тишины, `Step::Notes` — шагу нот, `Step::Done` — шагу итога; при
  /// `Step::Off` ничего не делает. После шага сохраняет его состояние и при
  /// необходимости выключает его и запускает следующий — тот начинает со
  /// следующего блока; когда шаг итога услышал звук, останавливает калибратор
  /// (`stop`).
  void receive(const input::AudioBlock &block) override;

private:
  /// Отдаёт блок `block` текущему шагу и подводит ход после него. Зовётся под
  /// `mutex_`.
  void dispatch(const input::AudioBlock &block);

  /// Отдаёт шагам `count` сэмплов тишины блоками, первый — со временем
  /// `start`. Зовётся под `mutex_`.
  void dispatchSilence(std::uint64_t count, Moment start);

  /// Сохраняет состояние шага тишины в состоянии калибратора. Зовётся под
  /// `mutex_`.
  SilenceStep::State syncSilence();

  /// Выключает шаги и переводит калибратор в `Step::Off`. Зовётся под
  /// `mutex_`.
  void stopSteps();

  // Детектор шагов и его частота: шаги держат детектор по ссылке.
  detector::Detector detector_;
  SampleRate rate_;

  // Шаги; их состояние держат их замки.
  SilenceStep silenceStep_;
  NotesStep notesStep_;
  ResultStep resultStep_;

  // Состояние калибратора — под этим замком.
  mutable std::mutex mutex_;
  State state_;
  // Конец полученного звука; пусто — блоков после запуска ещё не было.
  std::optional<Moment> heardEnd_;
};

} // namespace training::core::calibration
