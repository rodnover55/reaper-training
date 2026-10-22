#pragma once

// Сессия калибровки входа в связке (`input-calibration`): от запуска
// калибровки до её конца, поверх калибратора ядра.

#include "training/core/calibration/calibrator.hpp"
#include "training/core/detector/detector.hpp"
#include "training/core/input/audio_block.hpp"
#include "training/core/transport.hpp"
#include "training/core/types.hpp"

#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>

namespace training::reaper {

/// Обработчик событий калибровки (`Calibration`): что калибровка решила и
/// почему. Калибровка зовёт его методы по одному на событие, в порядке
/// событий, в потоке того своего вызова, который привёл к событию.
class CalibrationEvents {
public:
  /// Позволяет удалять реализацию через указатель на `CalibrationEvents`.
  virtual ~CalibrationEvents() = default;

  /// Создаёт обработчик; своих данных у `CalibrationEvents` нет, их хранит
  /// реализация.
  CalibrationEvents() = default;

  /// Копии нет: калибровка держит обработчик по указателю.
  CalibrationEvents(const CalibrationEvents &) = delete;

  /// Присваивания копией нет: калибровка держит обработчик по указателю.
  CalibrationEvents &operator=(const CalibrationEvents &) = delete;

  /// Перемещения нет: калибровка держит обработчик по указателю.
  CalibrationEvents(CalibrationEvents &&) = delete;

  /// Присваивания перемещением нет: калибровка держит обработчик по указателю.
  CalibrationEvents &operator=(CalibrationEvents &&) = delete;

  /// Сессия калибровки кончилась; это последнее событие сессии.
  virtual void ended() = 0;

  /// Запуск транспорта прервал шаги калибровки; следом идёт `ended`.
  virtual void cancelledByPlayback() = 0;

  /// Запуск транспорта закрыл кончившуюся калибровку; следом идёт `ended`.
  virtual void closedByPlayback() = 0;

  /// Калибровка началась с шага тишины на новой частоте дискретизации: пришёл
  /// первый блок сессии или блок другой частоты до итога.
  ///
  /// @param sampleRate частота дискретизации блока, Гц.
  virtual void silenceStep(double sampleRate) = 0;

  /// На шаге тишины был звук: шаг тишины отсчитывается заново.
  virtual void soundDuringSilence() = 0;

  /// Шаг тишины кончился, начался шаг нот.
  ///
  /// @param noiseDb уровень шума, dBFS.
  virtual void notesStep(double noiseDb) = 0;

  /// На шаге нот найдена нота.
  ///
  /// @param note номер ноты в сессии, 0 — первая.
  /// @param attackDb уровень атаки ноты, dBFS.
  virtual void attack(std::size_t note, double attackDb) = 0;

  /// Калибровка подвела итог; при удачном итоге следом идёт `silenceSet`.
  ///
  /// @param state состояние калибратора на шаге итога.
  virtual void finished(const core::calibration::Calibrator::State &state) = 0;

  /// Удачный итог калибровки поставил порог тишины.
  ///
  /// @param silenceDb поставленный порог тишины, dBFS, в допустимом
  ///   диапазоне калибровки (`Calibration::Calibration`).
  virtual void silenceSet(double silenceDb) = 0;

  /// «Apply anyway» поставил порог тишины; следом идёт `ended`.
  ///
  /// @param silenceDb поставленный порог тишины, dBFS, в допустимом
  ///   диапазоне калибровки (`Calibration::Calibration`).
  virtual void appliedAnyway(double silenceDb) = 0;

  /// Удар по струнам после итога закрыл сессию; следом идёт `ended`.
  virtual void closedBySound() = 0;
};

/// Калибровка входа в связке: сессии калибровки от запуска до конца поверх
/// калибратора ядра (`core::calibration::Calibrator`) и то, прервал ли
/// калибровку запуск транспорта.
///
/// Калибратор сессии запускает транспорт ядра (`core::Transport::start`),
/// который ставит вызывающий (`setTransport`). Первый блок сессии и блок
/// другой частоты до итога начинают калибровку заново, с шага тишины:
/// транспорт с частотой блока запускает новый калибратор. Блоки частоты,
/// которой у транспорта нет, в калибровку не идут; после итога звук другой
/// частоты в калибровку тоже не идёт. Если транспорт калибратор не запустил
/// (`core::CannotStartUnit`), сессия кончается.
///
/// Сессия кончается Finish, Close, Cancel, «Apply anyway», закрытием окна
/// (`end`), сменой входа (`onInputChange`), запуском транспорта (`onStart`) и
/// ударом по струнам после итога (`receive`).
///
/// Созданный объект: сессии нет, калибровку запуск не прерывал, частоты и
/// транспорта нет.
///
/// Многопоточность: все методы можно звать из любых потоков одновременно.
class Calibration final {
public:
  /// @param silenceRange допустимый порог тишины, dBFS: порог итога за краем
  ///   диапазона ставится на этом краю.
  /// @param events обработчик событий калибровки; nullptr — обработчика нет.
  ///   Обработчик остаётся во владении вызывающего и живёт дольше калибровки.
  explicit Calibration(core::Range<double> silenceRange, CalibrationEvents *events = nullptr);

  /// Ставит транспорт ядра, который запускает калибратор сессии. Запущенный
  /// калибратор остаётся до блока другой частоты.
  ///
  /// @param transport транспорт; nullptr — транспорта нет, и калибровка блоков
  ///   не слушает. Транспорт живёт до следующего вызова.
  void setTransport(core::Transport *transport);

  /// Начинает сессию калибровки; прежняя сессия кончается без изменений
  /// (`end`).
  void start();

  /// Кончает сессию без изменений порога тишины; поставленный удачной
  /// калибровкой порог остаётся. Без сессии ничего не делает.
  void end();

  /// Отмечает смену входа: сессия кончается без изменений порога тишины, как
  /// `end`, — она меряла прежний вход. Отметку прерванной запуском калибровки
  /// не трогает.
  void onInputChange();

  /// Отмечает начало запуска транспорта: оно кончает сессию. Шаги калибровки
  /// прерываются — до конца запуска это видно в `cancelledByPlayback`, — а
  /// кончившаяся калибровка просто закрывается. Без сессии ничего не делает.
  void onStart();

  /// Отмечает конец запуска транспорта: снимает отметку прерванной запуском
  /// калибровки.
  void onStop();

  /// Ставит частоту дискретизации следующих блоков. Калибровку под новую
  /// частоту перестраивает `receive`.
  void setSampleRate(core::SampleRate rate);

  /// Принимает следующий блок звука выбранного канала, снятого при
  /// остановленном транспорте, и подводит ход сессии после него: итог
  /// сообщается обработчику, удачный — вместе с порогом тишины (`silenceDb`);
  /// удар по струнам после итога кончает сессию. Без сессии, без частоты и с
  /// пустым блоком ничего не делает.
  void receive(const core::input::AudioBlock &block);

  /// «Apply anyway»: ставит порог тишины, посчитанный калибровкой, хотя она
  /// его не поставила, и кончает сессию.
  ///
  /// @return порог тишины, dBFS, в допустимом диапазоне; пусто — итога нет или
  ///   в нём нет ни одной ноты, и ничего не меняется.
  std::optional<double> applyAnyway();

  /// Правда, если сессия есть: звук при остановленном транспорте надо
  /// подавать в `receive`.
  bool listening() const;

  /// Состояние калибратора ядра; пусто, если сессии нет или в ней ещё не было
  /// звука: до него неизвестна частота дискретизации.
  std::optional<core::calibration::Calibrator::State> state() const;

  /// Правда, если сессия есть и калибровка подвела итог.
  bool finished() const;

  /// Правда, если на шаге тишины текущей калибровки был звук: время с
  /// последнего звука (`core::calibration::Calibrator::State::sinceSound`)
  /// уменьшилось — шаг тишины начат заново.
  bool heardSound() const;

  /// Порог тишины, который поставил удачный итог калибровки, dBFS, в
  /// допустимом диапазоне: середина за краем диапазона — на этом краю. Пусто —
  /// сессии нет, итога нет или итог неудачный.
  std::optional<double> silenceDb() const;

  /// Порог тишины, который поставит «Apply anyway» (`applyAnyway`), dBFS, в
  /// допустимом диапазоне. Есть, если калибровка подвела итог по всем нотам,
  /// но порог не поставила: шум слишком близко к атаке или вход перегружен.
  std::optional<double> anywaySilenceDb() const;

  /// Правда, если калибровку прервал запуск транспорта и он ещё идёт.
  bool cancelledByPlayback() const;

private:
  /// Кончает сессию, как `end`. Зовётся под `mutex_`.
  void endSession();

  /// Состояние калибратора; пусто без калибратора. Зовётся под `mutex_`.
  std::optional<core::calibration::Calibrator::State> current() const;

  /// Правда, если калибровка подвела итог. Зовётся под `mutex_`.
  bool done() const;

  /// Начинает калибровку заново: транспорт запускает новый калибратор своей
  /// частоты с шага тишины. Транспорт есть. Зовётся под `mutex_`.
  ///
  /// @return ложь, если транспорт калибратор не запустил
  ///   (`core::CannotStartUnit`).
  bool restart();

  /// Сообщает обработчику, что изменилось между состояниями `before` и
  /// `after` калибратора, и кончает сессию после удара по струнам. Зовётся
  /// под `mutex_`.
  void report(const core::calibration::Calibrator::State &before,
              const core::calibration::Calibrator::State &after);

  /// Порог тишины итога `thresholdDb` в допустимом диапазоне: порог за краем
  /// диапазона — на этом краю.
  double silenceOf(double thresholdDb) const;

  core::Range<double> silenceRange_;
  CalibrationEvents *events_ = nullptr;

  // Всё состояние — под этим замком.
  mutable std::mutex mutex_;
  bool session_ = false;
  std::optional<core::SampleRate> rate_;
  core::Transport *transport_ = nullptr;
  // Калибратор, запущенный сессией; пусто — сессия его ещё не запускала.
  std::shared_ptr<core::calibration::Calibrator> calibrator_;
  bool heardSound_ = false;
  bool cancelledByPlayback_ = false;
};

} // namespace training::reaper
