#pragma once

#include "training/core/detector/detector.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace training::test {

/// Дописывает в конец `onsets` все непрочитанные атаки детектора в порядке
/// начала и забирает их из детектора.
inline void takeOnsets(core::detector::Detector &detector,
                       std::vector<core::detector::Onset> &onsets) {
  while (const std::optional<core::detector::Onset> onset = detector.takeOnset())
    onsets.push_back(*onset);
}

/// Частота дискретизации `hz`, Гц; больше нуля.
inline core::SampleRate rateOf(double hz) { return *core::SampleRate::create(hz); }

/// Детектор с параметрами `settings`; частота среза в его промежутке.
inline core::detector::Detector detectorOf(const core::detector::Settings &settings) {
  return std::move(*core::detector::Detector::create(settings));
}

} // namespace training::test
