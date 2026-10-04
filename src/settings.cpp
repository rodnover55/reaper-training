#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_GetExtState
#define REAPERAPI_WANT_SetExtState

#include "settings.hpp"

// SWELL объявляет max и min макросами, и они ломают стандартную библиотеку.
#define WDL_NO_DEFINE_MINMAX

#include <reaper_plugin.h>
#include <reaper_plugin_functions.h>

#include <fmt/format.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace training::reaper {
namespace {

/// Раздел настроек расширения в `reaper-extstate.ini`.
constexpr const char *kSection = "reaper_training";

/// Целое из сохранённой строки; пусто — строки нет или в ней не целое.
/// Дробные `from_chars` не берутся: на macOS 12 их нет в системной
/// библиотеке C++.
std::optional<int> numberOf(const char *key) {
  const char *text = GetExtState(kSection, key);
  if (!text || !*text)
    return std::nullopt;

  const std::string_view view(text);
  int value = 0;
  const auto [end, error] = std::from_chars(view.data(), view.data() + view.size(), value);
  if (error != std::errc() || end != view.data() + view.size())
    return std::nullopt;
  return value;
}

void store(const char *key, const std::string &value) {
  SetExtState(kSection, key, value.c_str(), true);
}

} // namespace

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

  settings.toleranceMs = std::clamp(settings.toleranceMs, 1.0, 50.0);
  settings.silenceDb = std::clamp(settings.silenceDb, -90.0, -10.0);
  settings.channel = std::max(settings.channel, 0);
  return settings;
}

Settings loadSettings() {
  Settings settings;
  if (const auto mode = numberOf("mode"))
    settings.mode = static_cast<grid::Mode>(*mode);
  if (const auto tolerance = numberOf("tolerance_ms"))
    settings.toleranceMs = *tolerance;
  if (const auto channel = numberOf("channel"))
    settings.channel = *channel - 1;
  if (const auto silence = numberOf("silence_db"))
    settings.silenceDb = *silence;
  return clamped(settings);
}

void saveSettings(const Settings &settings) {
  store("mode", fmt::format("{}", grid::divisions(settings.mode)));
  store("tolerance_ms",
        fmt::format("{}", static_cast<int>(std::lround(settings.toleranceMs))));
  store("channel", fmt::format("{}", settings.channel + 1));
  store("silence_db", fmt::format("{}", static_cast<int>(std::lround(settings.silenceDb))));
}

} // namespace training::reaper
