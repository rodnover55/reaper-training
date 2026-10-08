#pragma once

// Настройки тренажёра (`timing-display`): общие для всех проектов.

#include "training/grid/hit_window.hpp"
#include "training/grid/mode.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace training::trainer {

/// Допустимые значения числа: от `min` до `max` включительно.
struct Range {
  double min = 0.0;
  double max = 0.0;

  /// Правда, если `value` не меньше `min` и не больше `max`.
  bool contains(double value) const { return value >= min && value <= max; }

  bool operator==(const Range &) const = default;
};

/// Настройки тренажёра. Значение каждой настройки допустимо: сеттер
/// проверяемой настройки отклоняет недопустимое значение, и настройки
/// остаются прежними.
///
/// Созданный объект: «четверти», шкала от −50 до +50 мс, окно попадания
/// 0 ± 10 мс, порог тишины −50 dBFS, первый канал звуковой карты без входа
/// MIDI, обе колонки строк показаны, панель настроек развёрнута. Окно
/// попадания по умолчанию лежит в любой допустимой шкале.
class Settings {
public:
  /// Допустимый порог тишины, dBFS: целые от −90 до −10.
  static constexpr Range kSilenceRange{.min = -90.0, .max = -10.0};

  /// Допустимая граница шкалы окна попадания, мс: целые от 10 до 200.
  static constexpr Range kScaleRange{.min = 10.0, .max = 200.0};

  /// Режим сетки.
  grid::Mode mode() const { return mode_; }

  /// Ставит режим сетки.
  ///
  /// @return ложь, если `mode` — не один из пяти режимов `grid::Mode`:
  ///   настройки не меняются.
  bool setMode(grid::Mode mode);

  /// Граница шкалы окна попадания, мс: шкала от −`scaleMs` до +`scaleMs`.
  /// Скрытая настройка: в окне её нет, тренажёр её не сохраняет.
  double scaleMs() const { return scaleMs_; }

  /// Ставит границу шкалы окна попадания.
  ///
  /// @return ложь, если `scaleMs` — не целое из `kScaleRange` или окно
  ///   попадания `window` не лежит в новой шкале (`grid::fits`): настройки не
  ///   меняются.
  bool setScaleMs(double scaleMs);

  /// Смещение, мс: центр окна попадания от клика; минус — до клика.
  double offsetMs() const { return window_.offsetMs; }

  /// Допуск, мс: половина ширины окна попадания.
  double toleranceMs() const { return window_.toleranceMs; }

  /// Окно попадания из смещения и допуска; лежит в шкале `scaleMs`
  /// (`grid::fits`).
  grid::HitWindow window() const { return window_; }

  /// Ставит окно попадания как есть: смещение и допуск не округляются.
  ///
  /// @return ложь, если окно не лежит в шкале `scaleMs` (`grid::fits`):
  ///   настройки не меняются.
  bool setWindow(const grid::HitWindow &window);

  /// Ставит смещение при прежнем допуске, как `setWindow`.
  ///
  /// @return ложь, если `offsetMs` вне `offsetRange`: настройки не меняются.
  bool setOffsetMs(double offsetMs);

  /// Ставит допуск при прежнем смещении, как `setWindow`.
  ///
  /// @return ложь, если `toleranceMs` вне `toleranceRange`: настройки не
  ///   меняются.
  bool setToleranceMs(double toleranceMs);

  /// Допустимое смещение при допуске и шкале настроек, мс: от
  /// −(`scaleMs` − `toleranceMs`) до +(`scaleMs` − `toleranceMs`).
  Range offsetRange() const;

  /// Допустимый допуск при смещении и шкале настроек, мс: от
  /// `grid::kMinToleranceMs` до `scaleMs` − |`offsetMs`|.
  Range toleranceRange() const;

  /// Порог тишины, dBFS.
  double silenceDb() const { return silenceDb_; }

  /// Ставит порог тишины.
  ///
  /// @return ложь, если `silenceDb` — не целое из `kSilenceRange`: настройки
  ///   не меняются.
  bool setSilenceDb(double silenceDb);

  /// Входной канал звуковой карты, 0 — первый. Пока выбран вход MIDI, не
  /// меняется: при возврате к звуку выбран тот же канал.
  int channel() const { return channel_; }

  /// Ставит входной канал звуковой карты.
  ///
  /// @return ложь, если `channel` меньше 0: настройки не меняются.
  bool setChannel(int channel);

  /// Выбранный вход MIDI — имя устройства для сохранения: имя без
  /// псевдонима, а если его нет, — с псевдонимом. Пусто — выбран канал
  /// звуковой карты `channel`.
  const std::string &midiInput() const { return midiInput_; }

  /// Ставит выбранный вход MIDI; пустое имя выбирает канал `channel`.
  void setMidiInput(std::string midiInput) { midiInput_ = std::move(midiInput); }

  /// Номер устройства MIDI у хоста, когда вход выбирали: различает два
  /// устройства с одним именем. Без входа MIDI не используется; по умолчанию
  /// −1.
  int midiIndex() const { return midiIndex_; }

  void setMidiIndex(int midiIndex) { midiIndex_ = midiIndex; }

  /// Колонка номеров тактов слева от строк («Bar numbers»).
  bool showBarNumbers() const { return showBarNumbers_; }

  void setShowBarNumbers(bool shown) { showBarNumbers_ = shown; }

  /// Колонка среднего и разброса справа от строк («Mean/spread»).
  bool showBarStats() const { return showBarStats_; }

  void setShowBarStats(bool shown) { showBarStats_ = shown; }

  /// Панель настроек свёрнута в одну строку: кнопку «Settings» и строку
  /// состояния.
  bool panelCollapsed() const { return panelCollapsed_; }

  void setPanelCollapsed(bool collapsed) { panelCollapsed_ = collapsed; }

  bool operator==(const Settings &) const = default;

private:
  grid::Mode mode_ = grid::Mode::Quarters;
  double scaleMs_ = grid::kDefaultScaleMs;
  grid::HitWindow window_{};
  double silenceDb_ = -50.0;
  std::string midiInput_{};
  int channel_ = 0;
  int midiIndex_ = -1;
  bool showBarNumbers_ = true;
  bool showBarStats_ = true;
  bool panelCollapsed_ = false;
};

/// Поле окна тренажёра, в которое вписывают число настройки.
enum class Field {
  /// Смещение окна попадания, мс.
  Offset,
  /// Допуск окна попадания, мс.
  Tolerance,
  /// Порог тишины, dBFS.
  Silence,
};

/// Подсказка к полю `field`: допустимые значения поля при остальных
/// настройках `settings` — «-40…+40 ms at tolerance 10», «1…50 ms at offset 0»,
/// «-90…-10 dBFS». Минус — дефис: в растровом шрифте диалогов Windows нет «−»
/// (U+2212).
std::string rangeHint(Field field, const Settings &settings);

/// Сообщение о том, что число `text`, вписанное в поле `field`, вне
/// допустимых значений и не принято: поле, вписанное как есть, допустимые
/// значения, как в `rangeHint`, и действующее значение настроек `settings` —
/// «Offset 45 ms is out of range -40…+40 at tolerance 10 — not applied, 0 in
/// use».
std::string rejectionText(Field field, std::string_view text, const Settings &settings);

/// Входной канал звуковой карты, звук которого нужен тренажёру при настройках
/// `settings`: `Settings::channel`; −1 — выбран вход MIDI, и звук не нужен.
int audioChannel(const Settings &settings);

/// Правда, если у настроек `before` и `after` разный выбранный вход: канал
/// звуковой карты, вход MIDI или номер его устройства.
bool inputChanged(const Settings &before, const Settings &after);

/// Выбранный вход настроек `settings` для журнала: «channel 1» или «MIDI
/// «KeyStep 37» #3».
std::string inputText(const Settings &settings);

} // namespace training::trainer
