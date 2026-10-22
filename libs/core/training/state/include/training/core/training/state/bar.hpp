#pragma once

// Такт запуска: доли такта во времени ядра и ноты, легшие в доли.

#include "training/core/clock.hpp"
#include "training/core/training/state/beat.hpp"
#include "training/core/training/state/meter.hpp"
#include "training/core/training/state/note.hpp"

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace training::core::training::state {

/// Такт запуска: где во времени ядра лежат его доли и какие ноты в них легли.
///
/// Многопоточность: все методы можно звать из любых потоков одновременно.
/// Значения отдаются копиями, снятыми под замком такта.
class Bar {
public:
  /// Создаёт такт с подписью `name`, началом `start`, концом `end`,
  /// размером `meter` и темпом `tempo` на доле 0, четвертей в минуту; долей и
  /// нот нет.
  Bar(std::string name, Moment start, Moment end, Meter meter, double tempo);

  /// Копия такта `other`, снятая под его замком.
  Bar(const Bar &other);

  /// Ставит значения такта `other`, снятые под его замком.
  Bar &operator=(const Bar &other);

  /// Название такта — любое, удобное пользователю: например «1.1», «2»,
  /// «Куплет».
  std::string name() const;

  /// Время, с которого такт действует в `Timeline`; более поздние такты могут
  /// перекрывать его (`Timeline::nearest`).
  Moment start() const;

  /// Время, когда такт кончается; при воспроизведении подряд равно `start`
  /// следующего такта. Такт, пришедший позже, перекрывает долю, только если её
  /// время от его `start` до его `end` (`Timeline::nearest`).
  Moment end() const;

  /// Правда, если время `time` лежит в такте: от `start` включительно до
  /// `end`.
  bool contains(Moment time) const;

  /// Размер такта: число ударов и их длительность.
  Meter meter() const;

  /// Темп такта по долям. Ключ — доля от начала такта; значение — темп,
  /// которым темп становится на этой доле, четвертей в минуту; у плавного
  /// перехода — темп в конце перехода. Доля 0 есть всегда: её значение —
  /// темп, действующий в начале такта.
  std::map<Beat, double> tempo() const;

  /// Доли такта — узлы сетки, по которым ядро меряет ноты: доля → её время.
  /// Время долей не раньше `start` и растёт вместе с местом доли.
  std::map<Beat, Moment> divisions() const;

  /// Ноты, легшие в доли: доля — ключ из `divisions` — и её ноты в порядке
  /// постановки. Доля без нот в map отсутствует.
  std::map<Beat, std::vector<Note>> notes() const;

  /// Ставит на доле `beat` темп `tempo`, четвертей в минуту: им темп
  /// становится на этой доле. На доле 0 — темп начала такта.
  void changeTempo(Beat beat, double tempo);

  /// Добавляет долю `beat` со временем `time`.
  ///
  /// @return ложь, и такт остаётся как есть, если доля `beat` уже есть,
  ///   `time` раньше `start` или время долей с ней не растёт вместе с местом
  ///   доли.
  bool addDivision(Beat beat, Moment time);

  /// Ставит ноту `note` в долю `beat` после её прошлых нот.
  ///
  /// @return ложь, и такт остаётся как есть, если доли `beat` у такта нет или
  ///   её время лежит вне такта (`contains`).
  bool addNote(Beat beat, const Note &note);

  /// Правда, если у тактов совпадают все поля.
  friend bool operator==(const Bar &left, const Bar &right);

private:
  // Список тактов читает доли своих тактов под своим замком: его такты
  // доступны только через него.
  friend class Timeline;

  /// Копия такта `other`; его замок держит вызывающий.
  Bar(const Bar &other, const std::scoped_lock<std::mutex> & /*lock*/);

  /// Правда, если время `time` лежит в такте. Зовётся под `mutex_` или
  /// замком списка тактов.
  bool holds(Moment time) const { return time >= start_ && time < end_; }

  // Значения такта — под этим замком.
  mutable std::mutex mutex_;
  std::string name_;
  Moment start_;
  Moment end_;
  Meter meter_;
  std::map<Beat, double> tempo_;
  std::map<Beat, Moment> divisions_;
  std::map<Beat, std::vector<Note>> notes_;
};

} // namespace training::core::training::state
