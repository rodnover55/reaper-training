#include "settings_read.hpp"

#include "training/reaper/half_ms.hpp"
#include "training/render/main/ms_text.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace training::reaper {
namespace {

/// Режим сетки с `value` долями на удар; пусто — такого режима нет.
std::optional<Mode> modeOf(int value) {
  for (const Mode mode : {Mode::Quarters, Mode::Eighths, Mode::EighthTriplets,
                          Mode::Sixteenths, Mode::SixteenthTriplets})
    if (divisions(mode) == value)
      return mode;
  return std::nullopt;
}

/// Окно попадания с краями `early` и `late` диапазоном: «+30…+50 ms».
std::string windowText(core::Interval early, core::Interval late) {
  return fmt::format("{}…{} ms", render::halfMsText(early, render::MsStyle::Signed),
                     render::halfMsText(late, render::MsStyle::Signed));
}

/// Целое из строки `key`; пусто — строки нет или в ней не целое.
std::optional<int> integerAt(const SettingsLookup &lookup, std::string_view key) {
  const std::optional<std::string> text = lookup(key);
  return text ? integerOf(*text) : std::nullopt;
}

/// Читает окно попадания из `offset_ms` и `tolerance_ms` в настройки ядра
/// `read.settings` по уже прочитанной шкале `read.view`; не принятое окно
/// называет в `read.rejected`.
void readWindow(const SettingsLookup &lookup, SettingsRead &read) {
  const core::HitWindow &inUse = read.settings.window();
  core::Interval offset = inUse.offset();
  core::Interval tolerance = inUse.tolerance();
  // Строки окна, которые есть в файле и где число: «offset_ms=40».
  std::vector<std::string> lines;
  if (const std::optional<std::string> text = lookup("offset_ms"))
    if (const std::optional<core::Interval> value = halfMsOf(*text)) {
      offset = *value;
      lines.push_back(fmt::format("offset_ms={}", *text));
    }
  if (const std::optional<std::string> text = lookup("tolerance_ms"))
    if (const std::optional<core::Interval> value = halfMsOf(*text)) {
      tolerance = *value;
      lines.push_back(fmt::format("tolerance_ms={}", *text));
    }
  if (lines.empty())
    return;

  if (const std::optional<core::HitWindow> window = core::HitWindow::create(offset, tolerance);
      window && read.view.fits(*window)) {
    read.settings.setWindow(*window);
    return;
  }
  const core::Interval scale = read.view.scale();
  read.rejected.push_back(
      fmt::format("{}: window {} is not allowed at scale {}…{} — {} in use",
                  fmt::join(lines, ", "), windowText(offset - tolerance, offset + tolerance),
                  render::halfMsText(-scale, render::MsStyle::Signed),
                  render::halfMsText(scale, render::MsStyle::Signed),
                  windowText(inUse.early(), inUse.late())));
}

/// Читает режим сетки из `mode` в `read.mode`; не принятый режим называет в
/// `read.rejected`, и остаётся режим по умолчанию, «четверти».
void readMode(const SettingsLookup &lookup, SettingsRead &read) {
  const std::optional<std::string> text = lookup("mode");
  if (!text)
    return;
  const std::optional<int> mode = integerOf(*text);
  if (!mode)
    return;
  if (const std::optional<Mode> known = modeOf(*mode))
    read.mode = *known;
  else
    read.rejected.push_back(
        fmt::format("mode={} is not a grid mode — Quarters in use", *text));
}

/// Читает границу шкалы окна попадания из `window_scale_ms` в настройки
/// отрисовки `read.view` по окну попадания `read.settings`; не принятую
/// границу называет в `read.rejected`.
void readScale(const SettingsLookup &lookup, SettingsRead &read) {
  const std::optional<std::string> text = lookup("window_scale_ms");
  if (!text)
    return;
  const std::optional<int> scale = integerOf(*text);
  if (!scale || read.view.setScale(render::Milliseconds(*scale), read.settings.window()))
    return;
  read.rejected.push_back(
      fmt::format("window_scale_ms={} is out of range {}…{} — {} in use", *text,
                  render::ViewSettings::kScaleRange.min.count(),
                  render::ViewSettings::kScaleRange.max.count(),
                  std::lround(read.view.scale() / core::Interval::kMillisecond)));
}

/// Читает порог тишины из `silence_db` в настройки ядра `read.settings`; порог
/// вне `kSilenceRange` называет в `read.rejected`.
void readSilence(const SettingsLookup &lookup, SettingsRead &read) {
  const std::optional<std::string> text = lookup("silence_db");
  if (!text)
    return;
  const std::optional<int> silence = integerOf(*text);
  if (!silence)
    return;
  if (kSilenceRange.contains(*silence))
    read.settings.setSilenceDb(*silence);
  else
    read.rejected.push_back(fmt::format("silence_db={} is out of range {}…{} — {} in use",
                                        *text, std::lround(kSilenceRange.min),
                                        std::lround(kSilenceRange.max),
                                        std::lround(read.settings.silenceDb())));
}

/// Читает число скрытых тактов из `hidden_bars` в настройки отрисовки
/// `read.view`; число вне `render::ViewSettings::kHiddenBarsRange` называет в
/// `read.rejected`.
void readHiddenBars(const SettingsLookup &lookup, SettingsRead &read) {
  const std::optional<std::string> text = lookup("hidden_bars");
  if (!text)
    return;
  const std::optional<int> bars = integerOf(*text);
  if (!bars || read.view.setHiddenBars(*bars))
    return;
  read.rejected.push_back(fmt::format("hidden_bars={} is out of range {}…{} — {} in use",
                                      *text, render::ViewSettings::kHiddenBarsRange.min,
                                      render::ViewSettings::kHiddenBarsRange.max,
                                      read.view.hiddenBars()));
}

} // namespace

std::optional<int> integerOf(std::string_view text) {
  int value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || error != std::errc() || end != text.data() + text.size())
    return std::nullopt;
  return value;
}

SettingsRead readSettings(const SettingsLookup &lookup) {
  SettingsRead read;
  readMode(lookup, read);
  readScale(lookup, read);
  readWindow(lookup, read);
  readSilence(lookup, read);
  readHiddenBars(lookup, read);

  // Отклонённый канал — канал по умолчанию, без записи.
  if (const std::optional<int> channel = integerAt(lookup, "channel"))
    read.input.setChannel(*channel - 1);
  if (const std::optional<std::string> midi = lookup("midi_input")) {
    read.input.setMidiInput(*midi);
    read.input.setInput(midi->empty() ? Input::Audio : Input::Midi);
  }
  if (const std::optional<int> index = integerAt(lookup, "midi_input_index"))
    read.input.setMidiIndex(*index);
  if (const std::optional<int> numbers = integerAt(lookup, "show_bar_numbers"))
    read.view.setShowBarNumbers(*numbers != 0);
  if (const std::optional<int> stats = integerAt(lookup, "show_bar_stats"))
    read.view.setShowBarStats(*stats != 0);
  if (const std::optional<int> collapsed = integerAt(lookup, "settings_collapsed"))
    read.view.setPanelCollapsed(*collapsed != 0);
  return read;
}

} // namespace training::reaper
