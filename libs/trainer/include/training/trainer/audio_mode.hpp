#pragma once

// Режим звукового пути: куда идёт звук входа звуковой карты.

#include "training/trainer/calibration.hpp"
#include "training/trainer/transport.hpp"

namespace training::trainer {

/// Куда идёт звук входа звуковой карты.
enum class AudioMode {
  /// Звук не берётся: так вход начинает работу, пока режим не задан.
  Off,
  /// Звук идёт в поиск атак (`Transport::onsetSearch`): и при запуске
  /// транспорта, и на остановке — поиск слушает вход постоянно.
  Onsets,
  /// Звук идёт в калибровку входа (`Calibrator::feed`).
  Calibration,
};

/// Режим звукового пути: транспорт стоит, а калибровка ждёт звук
/// (`Calibrator::listening`) — `Calibration`; иначе — `Onsets`, и при запуске,
/// и на остановке: поиск атак слышит звук до Play и не принимает звон,
/// начавшийся до Play, за атаку на старте.
AudioMode audioModeOf(const Transport &transport, const Calibrator &calibrator);

} // namespace training::trainer
