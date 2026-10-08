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

#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace training::reaper {
namespace {

/// Раздел настроек расширения в `reaper-extstate.ini`.
constexpr const char *kSection = "reaper_training";

/// Сохранённая строка настройки `key`; пусто — строки нет или она пуста.
std::optional<std::string> textOf(std::string_view key) {
  const char *text = GetExtState(kSection, std::string(key).c_str());
  if (!text || !*text)
    return std::nullopt;
  return std::string(text);
}

void store(const char *key, const std::string &value) {
  SetExtState(kSection, key, value.c_str(), true);
}

} // namespace

bool consoleLogEnabled() {
  const std::optional<std::string> text = textOf("console_log");
  const std::optional<int> value = text ? trainer::integerOf(*text) : std::nullopt;
  return value && *value != 0;
}

trainer::SettingsRead loadSettings() { return trainer::readSettings(textOf); }

void saveSettings(const Settings &settings) {
  store("mode", fmt::format("{}", grid::divisions(settings.mode())));
  // Граница шкалы — скрытая настройка: окно её не меняет и не пишет.
  store("tolerance_ms", grid::halfMsText(settings.toleranceMs(), grid::MsStyle::Plain));
  store("offset_ms", grid::halfMsText(settings.offsetMs(), grid::MsStyle::Plain));
  store("channel", fmt::format("{}", settings.channel() + 1));
  // Пустой ключ входа MIDI при загрузке выбирает канал `channel`.
  store("midi_input", settings.midiInput());
  store("midi_input_index", settings.midiInput().empty()
                                ? std::string()
                                : fmt::format("{}", settings.midiIndex()));
  store("silence_db", fmt::format("{}", static_cast<int>(std::lround(settings.silenceDb()))));
  store("show_bar_numbers", settings.showBarNumbers() ? "1" : "0");
  store("show_bar_stats", settings.showBarStats() ? "1" : "0");
  store("settings_collapsed", settings.panelCollapsed() ? "1" : "0");
}

} // namespace training::reaper
