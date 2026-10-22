#pragma once

// Запуск шагов ядра (`training.core.Transport`): калибровки и тренировки.

#include "training/core/calibration/calibrator.hpp"
#include "training/core/errors.hpp"
#include "training/core/settings.hpp"
#include "training/core/training/audio.hpp"
#include "training/core/training/midi.hpp"
#include "training/core/training/trainer.hpp"
#include "training/core/types.hpp"

#include <expected>
#include <memory>
#include <mutex>

namespace training::core {

/// Запуск шагов ядра: создаёт и запускает один из шагов — калибровку или
/// тренировку. Запущенный шаг возвращается обработчиком (`Unit`): вызывающий
/// передаёт ему данные и берёт у него результат. Запущен не больше чем один
/// шаг: `start` сначала останавливает запущенный (`Transport.start`).
///
/// - **Частота дискретизации** у транспорта одна на всё время его жизни
///   (`Transport.sampleRate`): с ней создаются калибратор и тренировка звука
///   (`calibration::Calibrator::create`, `training::Audio::create`). На
///   другой частоте вызывающий создаёт новый транспорт и запускает на нём шаг
///   заново.
/// - **Обработчик** — общий указатель: шаг живёт, пока его держит транспорт
///   или вызывающий. Калибратор и тренировку звука `start` каждый раз создаёт
///   заново; прежний шаг остаётся у тех, кто его держит, и его методы можно
///   звать дальше. Тренировка MIDI создаётся вместе с транспортом и одна на
///   всё время его жизни.
///
/// Созданный объект: ни один шаг не запущен.
///
/// Многопоточность: все методы можно звать из любых потоков одновременно.
class Transport {
public:
  /// Запуск калибровки.
  struct Calibration {};

  /// Запуск тренировки с видом входного канала `type`.
  template <training::Trainer::Type type> struct Training {
    /// Вид входного канала.
    static constexpr training::Trainer::Type inputType = type;
  };

  /// Создаёт транспорт с настройками `settings` и частотой дискретизации
  /// звука `sampleRate`.
  Transport(const Settings &settings, SampleRate sampleRate);

  /// Копирование запрещено: шаги транспорта созданы под его настройки.
  Transport(const Transport &) = delete;

  /// Присваивание копией запрещено: шаги транспорта созданы под его
  /// настройки.
  Transport &operator=(const Transport &) = delete;

  /// Частота дискретизации звука калибровки и тренировки звука.
  SampleRate sampleRate() const { return sampleRate_; }

  /// Запускает калибровку: останавливает запущенный шаг, создаёт калибратор и
  /// запускает его.
  ///
  /// @return калибратор — обработчик калибровки; `CannotStartUnit`, если
  ///   калибратор на частоте транспорта не создаётся
  ///   (`calibration::Calibrator::create`), — тогда запущенного шага нет.
  std::expected<std::shared_ptr<calibration::Calibrator>, CannotStartUnit>
  start(Calibration unit);

  /// Запускает тренировку по звуку: останавливает запущенный шаг, создаёт
  /// тренировку звука и запускает её.
  ///
  /// @return тренировка звука — обработчик тренировки; `CannotStartUnit`,
  ///   если тренировка звука на частоте транспорта не создаётся
  ///   (`training::Audio::create`), — тогда запущенного шага нет.
  std::expected<std::shared_ptr<training::Audio>, CannotStartUnit>
  start(Training<training::Trainer::Type::Audio> unit);

  /// Запускает тренировку по MIDI: останавливает запущенный шаг и запускает
  /// тренировку MIDI.
  ///
  /// @return тренировка MIDI — обработчик тренировки; её запуск удаётся
  ///   всегда.
  std::expected<std::shared_ptr<training::Midi>, CannotStartUnit>
  start(Training<training::Trainer::Type::Midi> unit);

  /// Останавливает запущенный шаг.
  void stop();

  /// Ставит настройки `settings`: они действуют на идущий запуск без
  /// перезапуска самого запуска (`training::Trainer::setSettings`). Окно
  /// попадания — на следующие ноты тренировок. Новый порог тишины получает
  /// тренировка звука без пересоздания её детектора атак
  /// (`training::Audio::setSettings`); у калибровки порог свой
  /// (`calibration::NotesStep.start#isolated`).
  void setSettings(const Settings &settings);

private:
  /// Останавливает калибратор и тренировки. Зовётся под `mutex_`.
  void stopUnits();

  SampleRate sampleRate_;

  // Всё изменяемое состояние — под этим замком.
  mutable std::mutex mutex_;
  Settings settings_;
  std::shared_ptr<calibration::Calibrator> calibrator_;
  std::shared_ptr<training::Audio> audio_;
  std::shared_ptr<training::Midi> midi_;
};

} // namespace training::core
