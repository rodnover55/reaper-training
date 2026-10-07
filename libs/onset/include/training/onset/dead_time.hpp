#pragma once

// Ноты MIDI: начало ноты и мёртвое время после измеренной ноты (`hit-timing`,
// design.md D4 изменения add-midi-input).

#include "training/onset/detector.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace training::onset {

/// Правда, если MIDI-сообщение `message` — начало ноты: Note On (статус
/// 0x90–0x9F, любой из 16 каналов) с ненулевой громкостью. Note On с
/// громкостью 0 — это конец ноты. Сообщение короче трёх байт — не начало
/// ноты.
inline bool isNoteStart(std::span<const std::uint8_t> message) {
  return message.size() >= 3 && (message[0] & 0xF0U) == 0x90U && message[2] != 0;
}

/// Мёртвое время после измеренной ноты MIDI: решает, какие ноты измерять.
///
/// Измеренная нота начинает мёртвое время длиной `kLengthSeconds` от своего
/// времени, обе границы входят. Нота в мёртвом времени пропускается и его не
/// продлевает. Нота вне мёртвого времени, позже или раньше него, измеряется и
/// начинает своё.
///
/// Созданный объект и объект после `reset()` мёртвого времени не держат:
/// следующая нота измеряется.
///
/// Многопоточность: один экземпляр — один поток вызовов.
class DeadTime {
public:
  /// Длина мёртвого времени, с: та же, что у поиска атак звука, 40 мс.
  static constexpr double kLengthSeconds = Settings{}.deadTimeMs / 1000.0;

  /// Кончает мёртвое время: следующая нота измеряется. Нужен на конце прохода
  /// петли, при запуске транспорта и при смене источника нот.
  void reset() { start_.reset(); }

  /// Принимает ноту и решает, измерять ли её.
  ///
  /// @param time время ноты на шкале проекта, с. Ноты подаются в порядке
  ///   прихода.
  /// @return правда, если нота вне мёртвого времени: её надо измерить, и от
  ///   неё идёт новое мёртвое время; ложь, если нота в мёртвом времени и
  ///   пропускается.
  bool add(double time) {
    if (start_ && time >= *start_ && time - *start_ <= kLengthSeconds)
      return false;
    start_ = time;
    return true;
  }

  /// Время ноты, от которой идёт мёртвое время, на шкале проекта, с; пусто —
  /// мёртвого времени нет.
  std::optional<double> start() const { return start_; }

private:
  std::optional<double> start_;
};

} // namespace training::onset
