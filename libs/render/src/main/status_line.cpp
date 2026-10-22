#include "training/render/main/status_line.hpp"

#include <fmt/format.h>

namespace training::render {

std::string StatusLine::text(std::string_view mode, std::string_view window,
                             core::state::WorkMode workMode, bool calibrationCancelled) const {
  std::string line =
      fmt::format("{}, window {}, latency {:.1f} ms", mode, window,
                  static_cast<double>(compensation / core::Interval::kMillisecond));
  if (rate != 1.0)
    line += fmt::format(", rate {}", rate);

  switch (input) {
  case InputStatus::Present:
    line +=
        workMode == core::state::WorkMode::Run ? " — measuring" : " — waiting for playback";
    break;
  case InputStatus::NoInput:
    line += " — selected input is not available";
    break;
  case InputStatus::NotEnabled:
    line += fmt::format(" — MIDI input is not enabled in {} preferences", host);
    break;
  }
  if (calibrationCancelled)
    line += " · calibration cancelled: playback started";
  return line;
}

} // namespace training::render
