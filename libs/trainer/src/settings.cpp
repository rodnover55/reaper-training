#include "training/trainer/settings.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace training::trainer {

Settings clamped(Settings settings) {
  switch (settings.mode) {
  case grid::Mode::Quarters:
  case grid::Mode::Eighths:
  case grid::Mode::EighthTriplets:
  case grid::Mode::Sixteenths:
  case grid::Mode::SixteenthTriplets:
    break;
  default:
    settings.mode = grid::Mode::Quarters;
  }

  settings.scaleMs = std::clamp(std::round(settings.scaleMs), 10.0, 200.0);
  const grid::HitWindow window = grid::fitted(settings.window(), settings.scaleMs);
  settings.offsetMs = window.offsetMs;
  settings.toleranceMs = window.toleranceMs;
  settings.silenceDb = std::clamp(settings.silenceDb, -90.0, -10.0);
  settings.channel = std::max(settings.channel, 0);
  return settings;
}

int audioChannel(const Settings &settings) {
  return settings.midiInput.empty() ? settings.channel : -1;
}

bool inputChanged(const Settings &before, const Settings &after) {
  return before.channel != after.channel || before.midiInput != after.midiInput ||
         before.midiIndex != after.midiIndex;
}

std::string inputText(const Settings &settings) {
  if (settings.midiInput.empty())
    return fmt::format("channel {}", settings.channel + 1);
  return fmt::format("MIDI «{}» #{}", settings.midiInput, settings.midiIndex);
}

} // namespace training::trainer
