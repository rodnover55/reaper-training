#pragma once

// Вид входа, которым тренажёр слышит игру (`timing-display`).

namespace training::reaper {

/// Вид входа, которым тренажёр слышит игру.
enum class Input {
  /// Канал звуковой карты.
  Audio,
  /// Вход MIDI.
  Midi,
};

} // namespace training::reaper
