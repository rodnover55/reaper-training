#pragma once

// Настройки тренажёра в файле настроек REAPER (`timing-display`): какие строки
// приняты.

#include "input_settings.hpp"

#include "training/core/hit_window.hpp"
#include "training/core/settings.hpp"
#include "training/core/types.hpp"
#include "training/reaper/mode.hpp"
#include "training/render/main/view_settings.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace training::reaper {

/// Допустимый порог тишины, dBFS: от −90 до −10.
inline constexpr core::Range<double> kSilenceRange{.min = -90.0, .max = -10.0};

/// Окно попадания по умолчанию: 0 ± 10 мс.
inline constexpr core::HitWindow kDefaultWindow =
    *core::HitWindow::create(core::Interval(), 10 * core::Interval::kMillisecond);

/// Настройки ядра по умолчанию: окно попадания `kDefaultWindow`, порог тишины
/// −50 dBFS.
inline const core::Settings kDefaultSettings{kDefaultWindow, -50.0};

/// Строка настройки `key` из файла: текст после «=»; пусто — строки нет или
/// она пуста.
using SettingsLookup = std::function<std::optional<std::string>(std::string_view key)>;

/// Настройки, прочитанные из файла: значения принятых строк, у остальных
/// настроек — по умолчанию.
struct SettingsRead {
  /// Настройки ядра.
  core::Settings settings = kDefaultSettings;

  /// Выбранный вход у REAPER.
  InputSettings input;

  /// Режим сетки, по которому связка строит доли тактов.
  Mode mode = Mode::Quarters;

  /// Настройки отрисовки окна.
  render::ViewSettings view;

  /// Не принятые строки файла в порядке разбора, каждая с тем, что действует
  /// вместо неё: «mode=5 is not a grid mode — Quarters in use». Минус —
  /// дефис, как в `render::rangeHint`.
  std::vector<std::string> rejected;
};

/// Целое из строки файла: вся строка — десятичное целое, со знаком минус или
/// без; пусто — иначе: «», «+1», « 1», «1.5», «1e2».
std::optional<int> integerOf(std::string_view text);

/// Читает настройки из файла через `lookup` по ключам `mode`,
/// `window_scale_ms`, `offset_ms`, `tolerance_ms`, `silence_db`, `channel`
/// (номер с 1), `midi_input`, `midi_input_index`, `show_bar_numbers`,
/// `show_bar_stats`, `hidden_bars`, `settings_collapsed`. Вид входа — `Input::Midi`,
/// если строка `midi_input` непуста, иначе `Input::Audio`. Строка,
/// которую отклонил сеттер `core::Settings`, `InputSettings` или
/// `render::ViewSettings`, а также `mode` вне пяти режимов `Mode` оставляют
/// настройке значение по умолчанию и попадают в `SettingsRead::rejected`;
/// остальные строки принимаются.
///
/// - Разбор идёт в порядке: режим, граница шкалы, окно попадания, порог
///   тишины, скрытые такты, остальное. Окно попадания проверяется по уже прочитанной шкале
///   (`render::ViewSettings::fits`).
/// - Окно попадания собирается из `offset_ms` и `tolerance_ms`, округлённых
///   до 0.5 мс (`halfMsOf`); отсутствующая строка даёт значение по
///   умолчанию. Если окно не принято, одна запись называет обе его строки,
///   которые есть в файле.
/// - Строка, где не число (`integerOf`, `halfMsOf`), и отклонённый
///   `channel` молча оставляют значение по умолчанию: в `rejected` их нет.
SettingsRead readSettings(const SettingsLookup &lookup);

} // namespace training::reaper
