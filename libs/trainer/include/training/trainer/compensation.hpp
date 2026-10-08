#pragma once

// Компенсация задержки входов (`hit-timing`).

namespace training::trainer {

/// Компенсация задержки входов — сдвиг, на который хост ставит запись.
struct Compensation {
  /// Компенсация для звука, реальные секунды.
  double audio = 0.0;

  /// Частота звуковой карты, по которой снята компенсация звука, Гц; 0 —
  /// неизвестна, и компенсация звука — 0.
  double audioSampleRate = 0.0;

  /// Задержка ввода звука, с: сколько звук идёт от входа звуковой карты до
  /// тренажёра; 0, если частота неизвестна. Для замера задержки показа.
  double inputLatency = 0.0;

  /// Компенсация для нот MIDI, реальные секунды.
  double midi = 0.0;

  /// Частота звуковой карты, по которой снята компенсация нот MIDI, Гц; 0 —
  /// неизвестна, и компенсация нот MIDI — 0.
  double midiSampleRate = 0.0;

  friend bool operator==(const Compensation &, const Compensation &) = default;
};

} // namespace training::trainer
