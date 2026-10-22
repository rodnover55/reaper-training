#pragma once

// Блок звука входа, который получает ядро.

#include "training/core/clock.hpp"

#include <span>

namespace training::core::input {

/// Блок сэмплов выбранного канала: подряд идущие сэмплы и время первого из
/// них. Длина блока — число сэмплов, делённое на частоту дискретизации
/// приёмника (`SoundReceiver::sampleRate`).
struct AudioBlock {
  /// Сэмплы в долях полной шкалы; их число — длина блока. Пустой блок несёт
  /// только время.
  std::span<const float> samples;

  /// Время начала блока — время первого сэмпла.
  Moment start;
};

} // namespace training::core::input
