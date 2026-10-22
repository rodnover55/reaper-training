#pragma once

// Тренировка (`training.core.training.Trainer`): ноты входа в долях тактов.

#include "training/core/clock.hpp"
#include "training/core/errors.hpp"
#include "training/core/hit_window.hpp"
#include "training/core/settings.hpp"
#include "training/core/training/state/bar.hpp"
#include "training/core/training/state/timeline.hpp"

#include <expected>
#include <mutex>
#include <vector>

namespace training::core::training {

/// Тренировка: ставит ноты входа в ближайшие доли тактов с оценкой по окну
/// попадания. Ноты даёт вид входа — звук (`Audio`) или MIDI (`Midi`) — через
/// `process`; такты — вызывающий (`addBar`).
///
/// Нота ждёт прихода такта, который начинается позже её времени: так
/// известно, к доле какого такта она ближе. Ждущие ноты ложатся по приходе
/// такта (`addBar`).
///
/// Созданный объект: тренировка не запущена (`Step::Off`), тактов нет.
///
/// Многопоточность: все методы можно звать из любых потоков одновременно.
class Trainer {
public:
  /// В каком состоянии тренировка. Разрешены переходы: `Off` → `Working` →
  /// `Off`.
  enum class Step {
    /// Тренировка не запущена.
    Off,
    /// Тренировка идёт.
    Working,
  };

  /// Состояние тренировки.
  struct State {
    /// В каком состоянии тренировка.
    Step step = Step::Off;

    /// Такты тренировки с нотами, легшими в их доли.
    state::Timeline timeline;
  };

  /// Нота входа, которую вид входа передаёт тренировке (`process`).
  struct Note {
    /// Время ноты.
    Moment time;

    /// Правда, если у нот одно время.
    friend bool operator==(const Note &, const Note &) = default;
  };

  /// Вид входа тренировки.
  enum class Type {
    /// Входящий поток звука (`Audio`).
    Audio,
    /// Входящий поток MIDI (`Midi`).
    Midi,
  };

  /// Позволяет удалять вид входа через указатель на `Trainer`.
  virtual ~Trainer() = default;

  /// Копирование запрещено: тренировка передаётся только
  /// по ссылке или указателю.
  Trainer(const Trainer &) = delete;

  /// Присваивание копией запрещено: тренировка передаётся
  /// только по ссылке или указателю.
  Trainer &operator=(const Trainer &) = delete;

  /// Перемещение запрещено: тренировка передаётся только
  /// по ссылке или указателю.
  Trainer(Trainer &&) = delete;

  /// Присваивание перемещением запрещено: тренировка передаётся
  /// только по ссылке или указателю.
  Trainer &operator=(Trainer &&) = delete;

  /// Запускает тренировку: сбрасывает состояние — такты и ждущие ноты — и
  /// переводит `Step::Off` → `Step::Working`.
  ///
  /// @return `InvalidStep`, если тренировка не в `Step::Off`.
  virtual std::expected<void, InvalidStep> start();

  /// Останавливает тренировку: переводит в `Step::Off`. Такты и легшие ноты
  /// остаются, ждущие ноты удаляются.
  void stop();

  /// Копия состояния тренировки.
  State result() const;

  /// Ставит настройки `settings`: окно попадания (`Settings::window`) — для
  /// следующих нот. Оценки легших нот остаются прежними.
  virtual void setSettings(const Settings &settings);

  /// Добавляет такт `bar` (`state::Timeline::add`) и ставит ноты, которые
  /// ждали такта для своей оценки.
  void addBar(const state::Bar &bar);

protected:
  /// Тренировка с окном попадания `hitWindow`.
  explicit Trainer(const HitWindow &hitWindow) : hitWindow_(hitWindow) {}

  /// Ставит ноту `note` в ближайшую долю тактов (`state::Timeline::nearest`)
  /// с оценкой по текущему окну попадания. До прихода такта, который
  /// начинается позже её времени, нота ждёт. Во время `Step::Off` нота
  /// отбрасывается.
  ///
  /// Зовут только виды входа — `Audio` и `Midi`.
  void process(const Note &note);

  /// Правда, если тренировка идёт (`Step::Working`).
  bool working() const;

private:
  /// Ставит ноту со временем `time` или оставляет ждать такта, как
  /// `process`. Зовётся под `mutex_`.
  void place(Moment time);

  // Всё состояние — под этим замком.
  mutable std::mutex mutex_;
  State state_;
  HitWindow hitWindow_;
  // Ноты, ждущие такта.
  std::vector<Moment> waiting_;
};

} // namespace training::core::training
