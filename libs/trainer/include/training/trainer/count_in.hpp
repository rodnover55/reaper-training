#pragma once

// Отсчёт метронома перед запуском — count-in (`hit-timing`).

#include "training/grid/timeline.hpp"

#include <cstdint>

namespace training::trainer {

/// Настройки отсчёта метронома перед запуском, как их хранит хост в проекте.
struct CountInSettings {
  /// Метроном включён: без него отсчёта нет.
  bool metronome = false;

  /// Отсчёт перед воспроизведением.
  bool beforePlayback = false;

  /// Отсчёт перед записью.
  bool beforeRecording = false;

  /// Длина отсчёта в тактах; не больше нуля — отсчёта нет.
  double bars = 0.0;

  /// Отсчёт начинается с начала такта: к его тактам добавляется путь от
  /// начала такта до точки запуска.
  bool fromBarStart = false;
};

/// Отсчёт запуска: сколько сэмплов звука хоста он длится.
struct CountIn {
  /// Длина отсчёта, сэмплов; 0 — отсчёта нет.
  std::uint64_t samples = 0;

  /// Длина отсчёта по тактам, реальные секунды: такты отсчёта, а при отсчёте с
  /// начала такта — ещё путь от начала такта до точки запуска, без округления
  /// до целого блока. Хост начинает отсчёт раньше, чем по тактам, на остаток
  /// до целого блока; клики отсчёта в этот остаток не попадают. 0 — отсчёта
  /// нет.
  double barsSeconds = 0.0;

  /// Частота звуковой карты, по которой посчитана длина, Гц; 0 — неизвестна,
  /// и отсчёта нет.
  double sampleRate = 0.0;

  /// Длина отсчёта, реальные секунды; 0, если частота неизвестна.
  double seconds() const {
    return sampleRate > 0.0 ? static_cast<double>(samples) / sampleRate : 0.0;
  }

  friend bool operator==(const CountIn &, const CountIn &) = default;
};

/// Отсчёт перед запуском. Отсчёт есть, если метроном включён и включён отсчёт
/// для этого запуска: перед записью или перед воспроизведением. Его длина —
/// такты отсчёта, умноженные на длину такта в точке запуска, а при отсчёте с
/// начала такта — ещё плюс путь от начала такта до точки запуска. Хост кончает
/// отсчёт на границе блока звука, поэтому длина округляется вверх до целого
/// числа блоков: 5.0 с при 48 кГц и блоке 512 сэмплов — 469 блоков, 240128
/// сэмплов.
///
/// @param settings настройки отсчёта проекта.
/// @param recording правда, если запуск — запись, иначе — воспроизведение.
/// @param barSeconds длина такта в точке запуска по её темпу и размеру,
///   реальные секунды; больше нуля.
/// @param sinceBarStart путь от начала такта до точки запуска, реальные
///   секунды; не меньше нуля.
/// @param sampleRate частота звуковой карты, Гц; не больше нуля — отсчёта
///   нет.
/// @param blockLength длина блока звука хоста, сэмплов; не больше нуля —
///   длина не округляется.
/// @return отсчёт; без отсчёта — `samples` = 0.
CountIn countInOf(const CountInSettings &settings, bool recording, double barSeconds,
                  double sinceBarStart, double sampleRate, int blockLength);

/// Такт карты `timeline`, в котором лежит позиция `position`. Хост отдаёт
/// позицию с точностью до сэмпла, и начало такта, которое не попадает на
/// целый сэмпл, приходит чуть раньше себя: позиция не дальше одного сэмпла
/// перед началом такта лежит в этом такте. При 110 BPM и 48 кГц начало такта
/// 5 — сэмпл 418909.09, а позиция 418909/48000 с — такт 5.
///
/// @param timeline карта темпа.
/// @param position позиция на шкале, с.
/// @param sampleRate частота звуковой карты, Гц; не больше нуля — допуска нет.
/// @return такт позиции.
grid::Bar barAt(const grid::Timeline &timeline, double position, double sampleRate);

/// Отсчёт перед запуском с позиции `position` (`countInOf`): длина такта и
/// путь от его начала — по темпу и размеру карты `timeline` в этой позиции,
/// такт позиции — `barAt`.
///
/// @param settings настройки отсчёта проекта.
/// @param recording правда, если запуск — запись, иначе — воспроизведение.
/// @param timeline карта темпа проекта.
/// @param position позиция начала запуска, с.
/// @param rate скорость воспроизведения проекта; больше нуля.
/// @param sampleRate частота звуковой карты, Гц.
/// @param blockLength длина блока звука хоста, сэмплов.
/// @return отсчёт; без отсчёта — `samples` = 0.
CountIn countInAt(const CountInSettings &settings, bool recording,
                  const grid::Timeline &timeline, double position, double rate,
                  double sampleRate, int blockLength);

/// Карта темпа запуска с отсчётом: раньше позиции начала запуска — сетка
/// отсчёта, с позиции начала — карта темпа проекта. Сетка отсчёта — темп и
/// размер в позиции начала, продолженные назад от такта, в котором она лежит
/// (`barAt`): по ним хост ставит клики отсчёта, даже если раньше в проекте
/// другие темп и размер. При одном темпе и размере — та же карта проекта.
///
/// Удары раньше позиции начала считаются от неё по длине удара в ней; такты —
/// от такта позиции начала назад, с его размером, без смен; их номера идут
/// назад от его номера и могут быть отрицательными.
class CountInTimeline final : public grid::Timeline {
public:
  /// @param project карта темпа проекта; живёт дольше этой карты.
  /// @param start позиция начала запуска, с.
  /// @param sampleRate частота звуковой карты, Гц: такт позиции начала —
  ///   `barAt`.
  CountInTimeline(const grid::Timeline &project, double start, double sampleRate);

  double beatsAt(double time) const override;
  double timeAt(double beats) const override;
  grid::Bar barOf(std::int64_t beat) const override;

private:
  const grid::Timeline &project_;
  double start_;
  double startBeats_;
  double beatSeconds_;
  grid::Bar anchor_;
};

} // namespace training::trainer
