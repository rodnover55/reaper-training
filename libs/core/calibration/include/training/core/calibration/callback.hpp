#pragma once

// Приём данных калибровкой (`training.core.calibration.Callback`).

#include "training/core/input/sound_receiver.hpp"

namespace training::core::calibration {

/// Интерфейс приёма калибровки: звук входа блоками.
class Callback : public input::SoundReceiver {};

} // namespace training::core::calibration
