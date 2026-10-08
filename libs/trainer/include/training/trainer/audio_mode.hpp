#pragma once

// Режим звукового пути: куда идёт звук входа звуковой карты.

#include "training/trainer/calibration.hpp"
#include "training/trainer/transport.hpp"

namespace training::trainer {

/// Куда идёт звук входа звуковой карты.
enum class AudioMode {
  /// Звук не берётся.
  Off,
  /// Звук идёт в поиск атак запуска транспорта (`Transport::onsetSearch`).
  Onsets,
  /// Звук идёт в калибровку входа (`Calibrator::feed`).
  Calibration,
};

/// Режим звукового пути: запуск транспорта идёт — `Onsets`; транспорт стоит, а
/// калибровка ждёт звук (`Calibrator::listening`) — `Calibration`; иначе —
/// `Off`.
AudioMode audioModeOf(const Transport &transport, const Calibrator &calibrator);

} // namespace training::trainer
