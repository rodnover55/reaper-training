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

/// Шаг тишины калибровки (`training::core::calibration::Step::Silence`):
/// гитарист глушит струны, шаг меряет шум. Измерение идёт по сигналу после
/// ВЧ-фильтра — по огибающей фильтра детектора (`detector::Detector::filter`).
///
/// Шаг слушает `kLength` звука после прогрева фильтра. Шум — наибольшая
/// огибающая окон шага. Если в тишине прозвучал звук громче обычного для шага
/// больше чем на 20 дБ, шаг отсчитывается заново после этого звука и его
/// затухания.
///
/// Созданный объект: шаг выключен.
///
/// Многопоточность: все методы можно звать из любых потоков одновременно.
class SilenceStep final : public input::SoundReceiver {
private:
  /// Сколько звука слушает шаг после прогрева фильтра.
  static constexpr Interval kLength = 3 * Interval::kSecond;

public:
  /// Состояние текущего запуска шага. Разрешены переходы: `Off` → `Loading`
  /// → `Working` → `Done`, и из `Loading`, `Working` и `Done` — в `Off`.
  enum class Step {
    /// Выключен, не работает.
    Off,
    /// Запускается: идёт прогрев фильтра.
    Loading,
    /// Выполняет вычисление.
    Working,
    /// Вычисление завершено.
    Done,
  };

  /// Состояние работы шага — копия на момент `result`.
  struct State {
    /// Текущее состояние шага.
    Step step = Step::Off;

    /// Уровень шума входящего сигнала, dBFS, готовый для тренажёра: во время
    /// `Step::Working` — текущее значение, во время `Step::Done` — финальное.
    double noiseDb = kFloorDb;

    /// Уровень текущего сигнала, dBFS: наибольшая огибающая последнего
    /// непустого блока во время `Step::Working`.
    double levelDb = kFloorDb;

    /// Время, которое прошло с последнего звука во время `Step::Working`;
    /// пока звука не было — с перехода в `Step::Working`.
    Interval sinceSound;

    /// Время, которое осталось до результата в `noiseDb` во время
    /// `Step::Working`.
    Interval silenceLeft = kLength;
  };

  /// Шаг блоков частоты `sampleRate`, который берёт фильтры у детектора
  /// `detector`; детектор живёт дольше шага, его частота — `sampleRate`.
  SilenceStep(detector::Detector &detector, SampleRate sampleRate);

  /// Частота дискретизации блоков шага.
  SampleRate sampleRate() const override { return rate_; }

  /// Обрабатывает блок сэмплов: при `Step::Working` обновляет `noiseDb`,
  /// `levelDb`, `sinceSound` и `silenceLeft`, в конце шага — финальный
  /// `noiseDb`. Звук, пропущенный между концом прошлого блока и началом
  /// этого, обрабатывается перед блоком как тишина; блок, начало которого
  /// лежит в уже полученном звуке, обрабатывается целиком. Сэмплы после конца
  /// шага не обрабатываются. Выключенный и завершённый шаг блоки не
  /// обрабатывает.
  void receive(const input::AudioBlock &block) override;

  /// Запускает шаг: переводит `Step::Off` → `Step::Loading`, ждёт прогрева
  /// фильтра и переводит `Step::Loading` → `Step::Working`.
  ///
  /// @return `InvalidStep`, если шаг не в `Step::Off`.
  std::expected<void, InvalidStep> start();

  /// Останавливает шаг и сбрасывает его состояние.
  void stop();

  /// Состояние шага на текущий момент.
  State result() const;

private:
  /// Обрабатывает следующие сэмплы потока `samples`; после конца шага ничего
  /// не делает. Зовётся под `mutex_`.
  ///
  /// @param filter фильтр шага `filter_`, который проверил вызывающий.
  void process(std::span<const float> samples, detector::Serial &filter);

  /// Закрывает окно шага. Когда окон не меньше пяти и среди них есть окно
  /// громче их медианы больше чем на 20 дБ, окна до последнего такого и оно
  /// само отбрасываются: шаг отсчитывается заново. Иначе, когда окон
  /// набралось на весь шаг, шум — наибольшее из них, и шаг завершён. Зовётся
  /// под `mutex_`.
  void closeWindow();

  /// Состояние шага. Зовётся под `mutex_`.
  State current() const;

  detector::Detector &detector_;
  SampleRate rate_;

  // Сколько сэмплов: прогрев фильтра, окно, окон в шаге.
  std::uint64_t settleLength_ = 0;
  std::uint64_t windowLength_ = 0;
  std::size_t windowsPerStep_ = 0;

  // Всё изменяемое состояние — под этим замком.
  mutable std::mutex mutex_;
  std::optional<detector::Serial> filter_;
  std::vector<double> buffer_;
  Step step_ = Step::Off;

  // Номер следующего сэмпла от запуска шага.
  std::uint64_t position_ = 0;

  // Конец полученного звука; пусто — блоков после запуска ещё не было.
  std::optional<Moment> heardEnd_;

  // Наибольшие огибающие закрытых окон и текущего окна; с какого сэмпла
  // отсчитывается время с последнего звука: переход в `Step::Working` или
  // последний звук.
  std::vector<double> windows_;
  double windowMax_ = 0.0;
  std::uint64_t windowFill_ = 0;
  std::uint64_t soundFrom_ = 0;

  double noiseDb_ = kFloorDb;
  double lastLevel_ = 0.0;
};

} // namespace training::core::calibration
