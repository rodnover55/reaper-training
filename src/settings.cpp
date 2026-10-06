#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_GetExtState
#define REAPERAPI_WANT_SetExtState

#include "settings.hpp"

// SWELL объявляет max и min макросами, и они ломают стандартную библиотеку.
#define WDL_NO_DEFINE_MINMAX

#include <reaper_plugin.h>
#include <reaper_plugin_functions.h>

#include "training/grid/hit_window.hpp"

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

/// Сохранённая строка настройки `key`; пусто — строки нет или она пуста.
std::optional<std::string_view> textOf(const char *key) {
  const char *text = GetExtState(kSection, key);
  if (!text || !*text)
    return std::nullopt;
  return std::string_view(text);
}

/// Целое из сохранённой строки; пусто — строки нет или в ней не целое.
/// Дробные `from_chars` не берутся: на macOS 12 их нет в системной
/// библиотеке C++.
std::optional<int> numberOf(const char *key) {
  const auto text = textOf(key);
  if (!text)
    return std::nullopt;

  const std::string_view view = *text;
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

  settings.scaleMs = std::clamp(std::round(settings.scaleMs), 10.0, 200.0);
  const grid::HitWindow window = grid::fitted(settings.window(), settings.scaleMs);
  settings.offsetMs = window.offsetMs;
  settings.toleranceMs = window.toleranceMs;
  settings.silenceDb = std::clamp(settings.silenceDb, -90.0, -10.0);
  settings.channel = std::max(settings.channel, 0);
  return settings;
}

bool consoleLogEnabled() {
  const auto value = numberOf("console_log");
  return value && *value != 0;
}

Settings loadSettings() {
  Settings settings;
  if (const auto mode = numberOf("mode"))
    settings.mode = static_cast<grid::Mode>(*mode);
  if (const auto scale = numberOf("window_scale_ms"))
    settings.scaleMs = *scale;
  if (const auto text = textOf("tolerance_ms"))
    if (const auto tolerance = grid::halfMsOf(*text))
      settings.toleranceMs = *tolerance;
  if (const auto text = textOf("offset_ms"))
    if (const auto offset = grid::halfMsOf(*text))
      settings.offsetMs = *offset;
  if (const auto channel = numberOf("channel"))
    settings.channel = *channel - 1;
  if (const auto silence = numberOf("silence_db"))
    settings.silenceDb = *silence;
  if (const auto numbers = numberOf("show_bar_numbers"))
    settings.showBarNumbers = *numbers != 0;
  if (const auto stats = numberOf("show_bar_stats"))
    settings.showBarStats = *stats != 0;
  if (const auto collapsed = numberOf("settings_collapsed"))
    settings.panelCollapsed = *collapsed != 0;
  return clamped(settings);
}

void saveSettings(const Settings &settings) {
  store("mode", fmt::format("{}", grid::divisions(settings.mode)));
  // Граница шкалы — скрытая настройка: окно её не меняет и не пишет.
  store("tolerance_ms", grid::halfMsText(settings.toleranceMs, grid::MsStyle::Plain));
  store("offset_ms", grid::halfMsText(settings.offsetMs, grid::MsStyle::Plain));
  store("channel", fmt::format("{}", settings.channel + 1));
  store("silence_db", fmt::format("{}", static_cast<int>(std::lround(settings.silenceDb))));
  store("show_bar_numbers", settings.showBarNumbers ? "1" : "0");
  store("show_bar_stats", settings.showBarStats ? "1" : "0");
  store("settings_collapsed", settings.panelCollapsed ? "1" : "0");
}

} // namespace training::reaper
