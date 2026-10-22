#include <doctest/doctest.h>

#include "settings_read.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using training::core::HitWindow;
using training::core::Interval;
using training::reaper::Input;
using training::reaper::InputSettings;
using training::reaper::integerOf;
using training::reaper::kDefaultSettings;
using training::reaper::Mode;
using training::reaper::readSettings;
using training::reaper::SettingsRead;
using training::render::ViewSettings;

namespace {

/// Читает настройки из файла со строками `lines`: ключ — текст после «=».
SettingsRead readFrom(const std::map<std::string, std::string, std::less<>> &lines) {
  return readSettings([&lines](std::string_view key) -> std::optional<std::string> {
    const auto found = lines.find(key);
    if (found == lines.end() || found->second.empty())
      return std::nullopt;
    return found->second;
  });
}

/// Окно попадания `offsetMs` ± `toleranceMs`.
HitWindow window(double offsetMs, double toleranceMs) {
  return *HitWindow::create(offsetMs * Interval::kMillisecond,
                            toleranceMs * Interval::kMillisecond);
}

/// Правда, если в прочитанном `read` все настройки по умолчанию.
bool defaults(const SettingsRead &read) {
  return read.settings == kDefaultSettings && read.input == InputSettings{} &&
         read.mode == Mode::Quarters && read.view == ViewSettings{};
}

} // namespace

TEST_CASE("Файл настроек: целое — вся строка") {
  CHECK(integerOf("5") == 5);
  CHECK(integerOf("-60") == -60);
  CHECK_FALSE(integerOf(""));
  CHECK_FALSE(integerOf("abc"));
  CHECK_FALSE(integerOf("1.5"));
  CHECK_FALSE(integerOf(" 1"));
  CHECK_FALSE(integerOf("+1"));
}

TEST_CASE("Файл настроек: все строки приняты") {
  const SettingsRead read = readFrom({{"mode", "4"},
                                      {"window_scale_ms", "100"},
                                      {"offset_ms", "60"},
                                      {"tolerance_ms", "30"},
                                      {"silence_db", "-60"},
                                      {"channel", "2"},
                                      {"midi_input", "KeyStep 37"},
                                      {"midi_input_index", "3"},
                                      {"show_bar_numbers", "0"},
                                      {"show_bar_stats", "0"},
                                      {"settings_collapsed", "1"}});
  CHECK(read.rejected.empty());
  CHECK(read.mode == Mode::Sixteenths);
  CHECK(read.view.scale() == 100 * Interval::kMillisecond);
  CHECK(read.settings.window() == window(60.0, 30.0));
  CHECK(read.settings.silenceDb() == -60.0);
  CHECK(read.input.input() == Input::Midi);
  CHECK(read.input.channel() == 1);
  CHECK(read.input.midiInput() == "KeyStep 37");
  CHECK(read.input.midiIndex() == 3);
  CHECK_FALSE(read.view.showBarNumbers());
  CHECK_FALSE(read.view.showBarStats());
  CHECK(read.view.panelCollapsed());
}

TEST_CASE("Файл настроек: пустой файл — настройки по умолчанию") {
  const SettingsRead read = readFrom({});
  CHECK(read.rejected.empty());
  CHECK(defaults(read));
}

TEST_CASE("Файл настроек: неизвестный режим не принят, остальное принято") {
  const SettingsRead read = readFrom({{"mode", "5"}, {"silence_db", "-60"}});
  CHECK(read.mode == Mode::Quarters);
  CHECK(read.settings.silenceDb() == -60.0);
  CHECK(read.rejected ==
        std::vector<std::string>{"mode=5 is not a grid mode — Quarters in use"});
}

TEST_CASE("Файл настроек: режим сетки — один из пяти по числу долей на удар") {
  const std::vector<std::pair<std::string, Mode>> modes{{"1", Mode::Quarters},
                                                        {"2", Mode::Eighths},
                                                        {"3", Mode::EighthTriplets},
                                                        {"4", Mode::Sixteenths},
                                                        {"6", Mode::SixteenthTriplets}};
  for (const auto &[text, mode] : modes) {
    const SettingsRead read = readFrom({{"mode", text}});
    CHECK(read.rejected.empty());
    CHECK(read.mode == mode);
  }
  CHECK(readFrom({{"mode", "0"}}).rejected.size() == 1);
}

TEST_CASE("Файл настроек: порог тишины вне диапазона — по умолчанию") {
  const SettingsRead read = readFrom({{"silence_db", "-5"}});
  CHECK(read.settings.silenceDb() == -50.0);
  CHECK(read.rejected ==
        std::vector<std::string>{"silence_db=-5 is out of range -90…-10 — -50 in use"});
}

TEST_CASE("Файл настроек: граница шкалы вне 10–200 — 50") {
  const SettingsRead read = readFrom({{"window_scale_ms", "500"}});
  CHECK(read.view.scale() == 50 * Interval::kMillisecond);
  CHECK(read.rejected ==
        std::vector<std::string>{"window_scale_ms=500 is out of range 10…200 — 50 in use"});
}

TEST_CASE("Файл настроек: окно не в шкале — 0 ± 10, одна запись об обеих строках") {
  const SettingsRead read =
      readFrom({{"offset_ms", "40"}, {"tolerance_ms", "10"}, {"window_scale_ms", "30"}});
  CHECK(read.view.scale() == 30 * Interval::kMillisecond);
  CHECK(read.settings.window() == window(0.0, 10.0));
  CHECK(read.rejected == std::vector<std::string>{
                             "offset_ms=40, tolerance_ms=10: window +30…+50 ms is not allowed "
                             "at scale -30…+30 — -10…+10 ms in use"});
}

TEST_CASE("Файл настроек: шкала шире — окно за прежней шкалой принято") {
  const SettingsRead read =
      readFrom({{"window_scale_ms", "100"}, {"offset_ms", "60"}, {"tolerance_ms", "30"}});
  CHECK(read.rejected.empty());
  CHECK(read.settings.window() == window(60.0, 30.0));
}

TEST_CASE("Файл настроек: допуск без смещения — смещение 0") {
  const SettingsRead read = readFrom({{"tolerance_ms", "15"}});
  CHECK(read.rejected.empty());
  CHECK(read.settings.window() == window(0.0, 15.0));
}

TEST_CASE("Файл настроек: окно округляется до 0.5 мс") {
  const SettingsRead read = readFrom({{"offset_ms", "7.3"}, {"tolerance_ms", "12.6"}});
  CHECK(read.rejected.empty());
  CHECK(read.settings.window() == window(7.5, 12.5));
}

TEST_CASE("Файл настроек: не число и канал меньше 1 — по умолчанию без записей") {
  const SettingsRead read =
      readFrom({{"silence_db", "abc"}, {"channel", "0"}, {"mode", "x"}, {"offset_ms", "far"}});
  CHECK(read.rejected.empty());
  CHECK(defaults(read));
}
