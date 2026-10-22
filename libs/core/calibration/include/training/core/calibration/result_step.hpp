#pragma once

#include "training/core/calibration/step.hpp"
#include "training/core/clock.hpp"
#include "training/core/detector/detector.hpp"
#include "training/core/detector/filter.hpp"
#include "training/core/input/sound_receiver.hpp"
#include "training/core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace training::core::calibration {

/// Шаг итога калибровки (`training::core::calibration::Step::Done`): завершает
/// процесс вычисления и ждёт звука, чтобы остановиться. Звук — огибающая
/// фильтра детектора (`detector::Detector::filter`) выше порога `thresholdDb`.
///
/// Созданный объект: шаг выключен.
///
/// Многопоточность: все методы можно звать из любых потоков одновременно.
class ResultStep final : public input::SoundReceiver {
public:
  /// Через какое время по умолчанию шаг начинает слушать сигнал.
  static constexpr Interval kDefaultWaitTime = 5 * Interval::kSecond;

  /// Состояние текущего запуска шага. Разрешены переходы: `Off` → `Loading`
  /// → `Working` → `Done`, и из `Loading`, `Working` и `Done` — в `Off`.
  enum class Step {
    /// Выключен, не работает.
    Off,
    /// Запускается: ждёт `waitTime` до начала прослушивания потока.
    Loading,
    /// Ждёт звука в потоке.
    Working,
    /// Вычисление завершено: звук услышан.
    Done,
  };

  /// Шаг блоков частоты `sampleRate`, который берёт фильтры у детектора
  /// `detector`; детектор живёт дольше шага, его частота — `sampleRate`.
  ///
  /// @param waitTime через какое время после запуска слушать сигнал.
  ResultStep(detector::Detector &detector, SampleRate sampleRate,
             Interval waitTime = kDefaultWaitTime);

  /// Текущее состояние шага.
  Step step() const;

  /// Через какое время после запуска шаг слушает сигнал.
  Interval waitTime() const { return waitTime_; }

  /// Порог сигнала, dBFS, по которому определяется звук: тот, что передан в
  /// `start`.
  double thresholdDb() const;

  /// Частота дискретизации блоков шага.
  SampleRate sampleRate() const override { return rate_; }

  /// Обрабатывает блок сэмплов: после `waitTime` от запуска переводит
  /// `Step::Loading` → `Step::Working` и ждёт звука, затем переводит
  /// `Step::Working` → `Step::Done`. Звук, пропущенный между концом прошлого
  /// блока и началом этого, обрабатывается перед блоком как тишина; блок,
  /// начало которого лежит в уже полученном звуке, обрабатывается целиком.
  /// Выключенный и завершённый шаг блоки не обрабатывает.
  void receive(const input::AudioBlock &block) override;

  /// Запускает шаг с порогом `thresholdDb`, dBFS: переводит `Step::Off` →
  /// `Step::Loading` и ждёт `waitTime`.
  ///
  /// @return `InvalidStep`, если шаг не в `Step::Off`.
  std::expected<void, InvalidStep> start(double thresholdDb);

  /// Останавливает шаг.
  void stop();

private:
  /// Обрабатывает следующие сэмплы потока `samples`; после конца шага ничего
  /// не делает. Зовётся под `mutex_`.
  ///
  /// @param filter фильтр шага `filter_`, который проверил вызывающий.
  void process(std::span<const float> samples, detector::Serial &filter);

  detector::Detector &detector_;
  SampleRate rate_;
  Interval waitTime_ = kDefaultWaitTime;

  // Всё изменяемое состояние — под этим замком.
  mutable std::mutex mutex_;
  std::optional<detector::Serial> filter_;
  std::vector<double> buffer_;
  Step step_ = Step::Off;
  double thresholdDb_ = kFloorDb;
  double threshold_ = 0.0;

  // Номер следующего сэмпла от запуска шага.
  std::uint64_t position_ = 0;

  // Конец полученного звука; пусто — блоков после запуска ещё не было.
  std::optional<Moment> heardEnd_;
};

} // namespace training::core::calibration
