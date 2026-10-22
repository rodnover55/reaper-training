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

/// Шаг нот калибровки (`training::core::calibration::Step::Notes`): гитарист
/// играет ноты по одной, шаг меряет уровень их атаки и пик сигнала.
///
/// Ноты ищет свой детектор шага `detector::Detector` с порогом тишины на 6 дБ
/// выше шума (`NotesStep.start#isolated`). Уровень атаки — наибольшая огибающая фильтра
/// детектора
/// (`detector::Detector::filter`) за 5 мс от начала ноты, то есть щелчок
/// медиатора; ноты сверх `notes` не в счёт. Шаг идёт, пока не найдено `notes`
/// нот и ещё 1 с после последней, или пока `silenceLength` нет новой ноты.
///
/// Созданный объект: шаг выключен.
///
/// Многопоточность: все методы можно звать из любых потоков одновременно.
class NotesStep final : public input::SoundReceiver {
private:
  /// Необходимое количество нот по умолчанию.
  static constexpr int kDefaultNotes = 8;

  /// Время без нот по умолчанию, после которого шаг останавливается.
  static constexpr Interval kDefaultSilenceLength = 15 * Interval::kSecond;

public:
  /// Состояние текущего запуска шага. Разрешены переходы: `Off` → `Loading`
  /// → `Working` → `Done`, и из `Loading`, `Working` и `Done` — в `Off`.
  enum class Step {
    /// Выключен, не работает.
    Off,
    /// Запускается: идёт прогрев фильтров.
    Loading,
    /// Выполняет вычисление.
    Working,
    /// Вычисление завершено.
    Done,
  };

  /// Чем закончилось измерение.
  enum class Verdict {
    /// Измерение завершено успешно.
    Good,
    /// Уровень шума слишком близко к звучащим нотам.
    NoiseTooClose,
    /// Вход перегружен.
    Clipping,
    /// Измерение оборвалось до получения необходимого количества нот.
    NotEnoughNotes,
  };

  /// Состояние работы шага — копия на момент `result`.
  struct State {
    /// Текущее состояние шага.
    Step step = Step::Off;

    /// Уровни атаки найденных нот, dBFS, в порядке нахождения, не больше
    /// `notes`: обновляются во время `Step::Working`, в `Step::Done`
    /// сохраняют последнее значение.
    std::vector<double> attacksDb;

    /// Время, которое прошло с последней ноты во время `Step::Working`; без
    /// нот — с перехода в `Step::Working`.
    Interval sinceLastNote;

    /// Уровень текущего сигнала, dBFS, во время `Step::Working`.
    double levelDb = kFloorDb;

    /// Пик сигнала, dBFS: наибольшая абсолютная величина входного сэмпла.
    /// Обновляется во время `Step::Working`, в `Step::Done` сохраняет
    /// последнее значение.
    double peakDb = kFloorDb;

    /// Итог измерения во время `Step::Done`.
    Verdict verdict = Verdict::NotEnoughNotes;

    /// Вычисленный порог тишины, dBFS, для определения нот: середина между
    /// шумом и самой тихой атакой, округлённая до целого. Вычисляется в
    /// `Step::Done`; без нот — `kFloorDb`.
    double thresholdDb = kFloorDb;
  };

  /// Шаг блоков частоты `sampleRate`. При запуске шаг создаёт свой детектор с
  /// настройками детектора `detector` и своим порогом тишины (`start`);
  /// частота `detector` — `sampleRate`. Ссылку на `detector` шаг не хранит.
  ///
  /// @param notes необходимое количество нот, больше нуля.
  /// @param silenceLength время без нот, после которого шаг останавливается.
  NotesStep(const detector::Detector &detector, SampleRate sampleRate,
            int notes = kDefaultNotes, Interval silenceLength = kDefaultSilenceLength);

  /// Необходимое количество нот.
  int notes() const { return notes_; }

  /// Время без нот, после которого шаг останавливается.
  Interval silenceLength() const { return silenceLength_; }

  /// Уровень шума, dBFS, переданный в `start`: шаг его не пересчитывает.
  double noiseDb() const;

  /// Частота дискретизации блоков шага.
  SampleRate sampleRate() const override { return rate_; }

  /// Обрабатывает блок сэмплов: при `Step::Working` обновляет `attacksDb`,
  /// `levelDb`, `peakDb` и `sinceLastNote`, в конце шага — `verdict` и
  /// `thresholdDb`. Звук, пропущенный между концом прошлого блока и началом
  /// этого, обрабатывается перед блоком как тишина; блок, начало которого
  /// лежит в уже полученном звуке, обрабатывается целиком. Атаки найденных
  /// нот и конец шага проверяются на границах, кратных длине атаки от запуска
  /// шага; сэмплы блока после конца шага остаются необработанными.
  /// Выключенный и завершённый шаг блоки не обрабатывает.
  void receive(const input::AudioBlock &block) override;

  /// Запускает шаг с уровнем шума `noiseDb`, dBFS: переводит `Step::Off` →
  /// `Step::Loading`, ждёт прогрева фильтров и переводит `Step::Loading` →
  /// `Step::Working`. Шаг создаёт свой детектор с порогом тишины на 6 дБ выше
  /// шума: поток детектора начинается с первого сэмпла шага, а порог
  /// держится до конца шага (`NotesStep.start#isolated`).
  ///
  /// @return `InvalidStep`, если шаг не в `Step::Off`.
  std::expected<void, InvalidStep> start(double noiseDb);

  /// Останавливает шаг и сбрасывает его состояние.
  void stop();

  /// Состояние шага на текущий момент.
  State result() const;

private:
  /// Сбрасывает ход шага. Зовётся под `mutex_`.
  void clear();

  /// Обрабатывает следующие сэмплы потока `samples`; после конца шага ничего
  /// не делает. Зовётся под `mutex_`.
  ///
  /// @param filter фильтр шага `filter_`, который проверил вызывающий.
  /// @param detector детектор шага `detector_`, который проверил вызывающий.
  void process(std::span<const float> samples, detector::Serial &filter,
               detector::Detector &detector);

  /// Измеряет атаки нот, все сэмплы которых уже прошли, и подводит итог
  /// (`finish`), когда после последней необходимой ноты прошла 1 с или
  /// `silenceLength` нет новой ноты. Зовётся под `mutex_`.
  void checkpoint();

  /// Подводит итог по шуму, атакам и пику и завершает шаг. Зовётся под
  /// `mutex_`.
  void finish();

  /// Состояние шага. Зовётся под `mutex_`.
  State current() const;

  // Настройки детектора, с которыми шаг создаёт свой детектор (`start`).
  detector::Settings detectorSettings_;
  SampleRate rate_;
  int notes_ = kDefaultNotes;
  Interval silenceLength_ = kDefaultSilenceLength;

  // Сколько сэмплов: прогрев фильтров, атака, история огибающей, ожидание
  // после последней ноты и обрыв без нот.
  std::uint64_t settleLength_ = 0;
  std::uint64_t attackLength_ = 0;
  std::uint64_t historyLength_ = 0;
  std::uint64_t afterLastLength_ = 0;
  std::uint64_t timeoutLength_ = 0;

  // Всё изменяемое состояние — под этим замком.
  mutable std::mutex mutex_;
  // Детектор шага и его фильтр: есть с запуска до остановки.
  std::optional<detector::Detector> detector_;
  std::optional<detector::Serial> filter_;
  std::vector<double> buffer_;
  Step step_ = Step::Off;
  double noiseDb_ = kFloorDb;

  // Номер следующего сэмпла от запуска шага.
  std::uint64_t position_ = 0;

  // Конец полученного звука; пусто — блоков после запуска ещё не было.
  std::optional<Moment> heardEnd_;

  // Огибающая последних `historyLength_` сэмплов кольцом по номеру сэмпла;
  // начала атак, уровень которых ещё меряется; пик; когда начался
  // `Step::Working` или пришла последняя нота; когда кончится шаг после
  // последней необходимой ноты.
  std::vector<double> history_;
  std::vector<std::uint64_t> pending_;
  std::vector<double> attacks_;
  double peak_ = 0.0;
  std::uint64_t lastNoteAt_ = 0;
  std::optional<std::uint64_t> finishAt_;

  double lastLevel_ = 0.0;
  Verdict verdict_ = Verdict::NotEnoughNotes;
  double thresholdDb_ = kFloorDb;
};

} // namespace training::core::calibration
