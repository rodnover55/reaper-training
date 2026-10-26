#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_GetExtState
#define REAPERAPI_WANT_SetExtState

#include "settings.hpp"

// SWELL объявляет max и min макросами, и они ломают стандартную библиотеку.
#define WDL_NO_DEFINE_MINMAX

#include <reaper_plugin.h>
#include <reaper_plugin_functions.h>

#include "training/render/main/ms_text.hpp"

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

/// Сохраняет строку `value` настройки `key` в разделе расширения
/// `reaper-extstate.ini`: запись остаётся после перезапуска REAPER.
void store(const char *key, const std::string &value) {
  SetExtState(kSection, key, value.c_str(), true);
}

} // namespace

bool consoleLogEnabled() {
  const std::optional<std::string> text = textOf("console_log");
  const std::optional<int> value = text ? integerOf(*text) : std::nullopt;
  return value && *value != 0;
}

SettingsRead loadSettings() { return readSettings(textOf); }

void saveSettings(const core::Settings &settings, const InputSettings &input, Mode mode,
                  const render::ViewSettings &view) {
  store("mode", fmt::format("{}", divisions(mode)));
  // Граница шкалы — скрытая настройка: окно её не меняет и не пишет.
  store("tolerance_ms",
        render::halfMsText(settings.window().tolerance(), render::MsStyle::Plain));
  store("offset_ms", render::halfMsText(settings.window().offset(), render::MsStyle::Plain));
  store("channel", fmt::format("{}", input.channel() + 1));
  // Пустой ключ входа MIDI при загрузке выбирает канал `channel`.
  const bool midi = input.input() == Input::Midi;
  store("midi_input", midi ? input.midiInput() : std::string());
  store("midi_input_index", midi ? fmt::format("{}", input.midiIndex()) : std::string());
  store("silence_db", fmt::format("{}", static_cast<int>(std::lround(settings.silenceDb()))));
  store("show_bar_numbers", view.showBarNumbers() ? "1" : "0");
  store("show_bar_stats", view.showBarStats() ? "1" : "0");
  store("hidden_bars", fmt::format("{}", view.hiddenBars()));
  store("settings_collapsed", view.panelCollapsed() ? "1" : "0");
}

} // namespace training::reaper
