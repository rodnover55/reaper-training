#pragma once

// Настройки тренажёра в файле настроек хоста (`timing-display`): какие строки
// приняты и сколько держится сообщение о не принятых.

#include "training/trainer/settings.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace training::trainer {

/// Строка настройки `key` из файла: текст после «=»; пусто — строки нет или
/// она пуста.
using SettingsLookup = std::function<std::optional<std::string>(std::string_view key)>;

/// Настройки, прочитанные из файла.
struct SettingsRead {
  /// Настройки: значения принятых строк, у остальных настроек — по умолчанию.
  Settings settings;

  /// Не принятые строки файла в порядке разбора, каждая с тем, что действует
  /// вместо неё: «mode=5 is not a grid mode — Quarters in use». Минус —
  /// дефис, как в `rangeHint`.
  std::vector<std::string> rejected;
};

/// Целое из строки файла: вся строка — десятичное целое, со знаком минус или
/// без; пусто — иначе: «», «+1», « 1», «1.5», «1e2».
std::optional<int> integerOf(std::string_view text);

/// Читает настройки из файла через `lookup` по ключам `mode`,
/// `window_scale_ms`, `offset_ms`, `tolerance_ms`, `silence_db`, `channel`
/// (номер с 1), `midi_input`, `midi_input_index`, `show_bar_numbers`,
/// `show_bar_stats`, `settings_collapsed`. Строка, которую не принял сеттер
/// `Settings`, оставляет настройке значение по умолчанию и попадает в
/// `SettingsRead::rejected`; остальные строки принимаются.
///
/// - Разбор идёт в порядке: режим, граница шкалы, окно попадания, порог
///   тишины, остальное. Окно попадания проверяется по уже прочитанной шкале.
/// - Окно попадания собирается из `offset_ms` и `tolerance_ms`, округлённых
///   до 0.5 мс (`grid::halfMsOf`); отсутствующая строка даёт значение по
///   умолчанию. Если окно не принято, одна запись называет обе его строки,
///   которые есть в файле.
/// - Строка, где не число (`integerOf`, `grid::halfMsOf`), и отклонённый
///   `channel` молча оставляют значение по умолчанию: в `rejected` их нет.
SettingsRead readSettings(const SettingsLookup &lookup);

/// Сообщение о не принятых строках файла в строке состояния окна: видно с
/// первого открытия окна до первой правки настроек в окне или до первого
/// запуска транспорта после этого открытия.
///
/// Созданный объект: сообщение ещё не видно.
///
/// Многопоточность: один поток вызовов.
class FileNotice {
public:
  /// Сообщения нет никогда.
  FileNotice() = default;

  /// @param file имя файла настроек для сообщения.
  /// @param rejected не принятые строки файла (`SettingsRead::rejected`);
  ///   пусто — сообщения нет никогда.
  FileNotice(std::string_view file, const std::vector<std::string> &rejected);

  /// Отмечает открытие окна: сообщение становится видно, если его ещё не
  /// сняли.
  void onWindowOpen();

  /// Отмечает правку настроек в окне: после `onWindowOpen` снимает
  /// сообщение насовсем, до него ничего не делает.
  void onSettingsEdit();

  /// Отмечает запуск транспорта: после `onWindowOpen` снимает сообщение
  /// насовсем, до него ничего не делает.
  void onTransportStart();

  /// Сообщение, пока оно видно: имя файла, «: » и не принятые строки через
  /// «; »; иначе пусто.
  std::string_view text() const;

private:
  void dismiss();

  std::string text_;
  bool opened_ = false;
  bool dismissed_ = false;
};

} // namespace training::trainer
