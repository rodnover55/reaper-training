#include "training/trainer/settings_file.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace training::trainer {
namespace {

/// Окно попадания диапазоном: «+30…+50 ms».
std::string windowText(const grid::HitWindow &window) {
  return fmt::format("{}…{} ms", grid::halfMsText(window.early(), grid::MsStyle::Signed),
                     grid::halfMsText(window.late(), grid::MsStyle::Signed));
}

/// Целое из строки `key`; пусто — строки нет или в ней не целое.
std::optional<int> integerAt(const SettingsLookup &lookup, std::string_view key) {
  const std::optional<std::string> text = lookup(key);
  return text ? integerOf(*text) : std::nullopt;
}

/// Читает окно попадания из `offset_ms` и `tolerance_ms` в настройки
/// `settings` с уже прочитанной шкалой; не принятое окно называет в
/// `rejected`.
void readWindow(const SettingsLookup &lookup, Settings &settings,
                std::vector<std::string> &rejected) {
  grid::HitWindow window = settings.window();
  // Строки окна, которые есть в файле и где число: «offset_ms=40».
  std::vector<std::string> lines;
  if (const std::optional<std::string> text = lookup("offset_ms"))
    if (const std::optional<double> offset = grid::halfMsOf(*text)) {
      window.offsetMs = *offset;
      lines.push_back(fmt::format("offset_ms={}", *text));
    }
  if (const std::optional<std::string> text = lookup("tolerance_ms"))
    if (const std::optional<double> tolerance = grid::halfMsOf(*text)) {
      window.toleranceMs = *tolerance;
      lines.push_back(fmt::format("tolerance_ms={}", *text));
    }

  if (lines.empty() || settings.setWindow(window))
    return;
  rejected.push_back(fmt::format("{}: window {} is not allowed at scale {}…{} — {} in use",
                                 fmt::join(lines, ", "), windowText(window),
                                 grid::halfMsText(-settings.scaleMs(), grid::MsStyle::Signed),
                                 grid::halfMsText(settings.scaleMs(), grid::MsStyle::Signed),
                                 windowText(settings.window())));
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
  Settings &settings = read.settings;

  if (const std::optional<std::string> text = lookup("mode"))
    if (const std::optional<int> mode = integerOf(*text))
      // Не принятый режим — режим по умолчанию, «четверти».
      if (!settings.setMode(static_cast<grid::Mode>(*mode)))
        read.rejected.push_back(
            fmt::format("mode={} is not a grid mode — Quarters in use", *text));

  if (const std::optional<std::string> text = lookup("window_scale_ms"))
    if (const std::optional<int> scale = integerOf(*text))
      if (!settings.setScaleMs(*scale))
        read.rejected.push_back(fmt::format(
            "window_scale_ms={} is out of range {}…{} — {} in use", *text,
            std::lround(Settings::kScaleRange.min), std::lround(Settings::kScaleRange.max),
            std::lround(settings.scaleMs())));

  readWindow(lookup, settings, read.rejected);

  if (const std::optional<std::string> text = lookup("silence_db"))
    if (const std::optional<int> silence = integerOf(*text))
      if (!settings.setSilenceDb(*silence))
        read.rejected.push_back(fmt::format("silence_db={} is out of range {}…{} — {} in use",
                                            *text, std::lround(Settings::kSilenceRange.min),
                                            std::lround(Settings::kSilenceRange.max),
                                            std::lround(settings.silenceDb())));

  // Отклонённый канал — канал по умолчанию, без записи.
  if (const std::optional<int> channel = integerAt(lookup, "channel"))
    settings.setChannel(*channel - 1);
  if (const std::optional<std::string> midi = lookup("midi_input"))
    settings.setMidiInput(*midi);
  if (const std::optional<int> index = integerAt(lookup, "midi_input_index"))
    settings.setMidiIndex(*index);
  if (const std::optional<int> numbers = integerAt(lookup, "show_bar_numbers"))
    settings.setShowBarNumbers(*numbers != 0);
  if (const std::optional<int> stats = integerAt(lookup, "show_bar_stats"))
    settings.setShowBarStats(*stats != 0);
  if (const std::optional<int> collapsed = integerAt(lookup, "settings_collapsed"))
    settings.setPanelCollapsed(*collapsed != 0);
  return read;
}

FileNotice::FileNotice(std::string_view file, const std::vector<std::string> &rejected) {
  if (!rejected.empty())
    text_ = fmt::format("{}: {}", file, fmt::join(rejected, "; "));
}

void FileNotice::onWindowOpen() { opened_ = true; }

void FileNotice::onSettingsEdit() { dismiss(); }

void FileNotice::onTransportStart() { dismiss(); }

void FileNotice::dismiss() {
  if (opened_)
    dismissed_ = true;
}

std::string_view FileNotice::text() const {
  if (!opened_ || dismissed_)
    return {};
  return text_;
}

} // namespace training::trainer
