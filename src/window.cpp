#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_GetMainHwnd
#define REAPERAPI_WANT_DockWindowAddEx
#define REAPERAPI_WANT_DockWindowRemove
#define REAPERAPI_WANT_DockWindowActivate
#define REAPERAPI_WANT_GetNumAudioInputs
#define REAPERAPI_WANT_GetInputChannelName

#include "window.hpp"

#include <reaper_plugin_functions.h>

// Строки в коде — UTF-8, а модуль на Windows собран без UNICODE: функции Win32
// с текстом там — ANSI-версии и читают его в кодовой странице системы.
// Заголовок подменяет SetDlgItemText и GetDlgItemText обёртками для UTF-8;
// DrawText и выпадающие списки требуют DrawTextUTF8 и WDL_UTF8_HookComboBox.
// На других ОС SWELL понимает UTF-8 сам, и обёртки — те же функции; там
// заголовку нужен wdltypes.h, который он не подключает сам.
#include <WDL/wdltypes.h>
#include <WDL/win32_utf8.h>

#include "journal.hpp"
#include "midi_inputs.hpp"
#include "trainer.hpp"
#include "views/ids.h"

#include "training/grid/display.hpp"
#include "training/grid/hit_window.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace training::reaper {
namespace {

REAPER_PLUGIN_HINSTANCE resourceModule = nullptr;

HWND window = nullptr;

/// Режимы в порядке списка в окне.
constexpr std::array<grid::Mode, 5> kModes{grid::Mode::Quarters, grid::Mode::Eighths,
                                           grid::Mode::EighthTriplets, grid::Mode::Sixteenths,
                                           grid::Mode::SixteenthTriplets};

constexpr std::array<const char *, 5> kModeNames{"Quarters", "Eighths", "Eighth triplets",
                                                 "Sixteenths", "Sixteenth triplets"};

/// Пока окно само пишет настройки в контролы, их уведомления не меняют
/// настройки обратно.
bool showingSettings = false;

int modeIndex(grid::Mode mode) {
  for (std::size_t i = 0; i < kModes.size(); ++i)
    if (kModes[i] == mode)
      return static_cast<int>(i);
  return 0;
}

/// Целое из текста; пусто — в тексте не целое.
std::optional<int> intOf(const char *text) {
  const std::string_view view(text);
  int value = 0;
  const auto [end, error] = std::from_chars(view.data(), view.data() + view.size(), value);
  if (view.empty() || error != std::errc() || end != view.data() + view.size())
    return std::nullopt;
  return value;
}

std::optional<int> editValue(HWND dialog, int control) {
  std::array<char, 32> text{};
  GetDlgItemText(dialog, control, text.data(), static_cast<int>(text.size()));
  return intOf(text.data());
}

/// Миллисекунды из поля `control`, округлённые до 0.5; пусто — в поле не
/// число.
std::optional<double> halfEditValue(HWND dialog, int control) {
  std::array<char, 32> text{};
  GetDlgItemText(dialog, control, text.data(), static_cast<int>(text.size()));
  return grid::halfMsOf(text.data());
}

/// Ставит в настройки `settings` смещение и допуск окна попадания `hit`.
void setWindow(Settings &settings, const grid::HitWindow &hit) {
  settings.offsetMs = hit.offsetMs;
  settings.toleranceMs = hit.toleranceMs;
}

/// Окно попадания диапазоном для контролов окна: «+4…+20 ms», «-5…+20 ms».
std::string rangeText(const grid::HitWindow &hit) {
  // Минус — дефис: контролы на Windows пишут растровым шрифтом диалога, в
  // котором есть «…», но нет «−» (U+2212).
  return fmt::format("{}…{} ms", grid::halfMsText(hit.early(), grid::MsStyle::Signed),
                     grid::halfMsText(hit.late(), grid::MsStyle::Signed));
}

/// Пишет настройки в журнал строкой «`what`: mode …»: по журналу проверки
/// видят их без снимка экрана.
void journalSettings(std::string_view what) {
  const Settings &settings = trainer().settings();
  journal("{}: mode {}, offset {} ms, tolerance {} ms, input {}, silence {} dBFS, bar "
          "numbers {}, mean/spread {}, collapsed {}",
          what, grid::divisions(settings.mode),
          grid::halfMsText(settings.offsetMs, grid::MsStyle::Plain),
          grid::halfMsText(settings.toleranceMs, grid::MsStyle::Plain), inputText(settings),
          settings.silenceDb, settings.showBarNumbers, settings.showBarStats,
          settings.panelCollapsed);
}

/// Что показывает панель калибровки.
enum class Panel {
  /// Шаг тишины, в том числе до первого звука.
  Silence,
  /// Шаг нот.
  Notes,
  /// Звук от звуковой карты не приходит.
  NoSound,
  /// Итог калибровки.
  Result,
};

Panel panelOf(const CalibrationSession &session) {
  if (session.noSound)
    return Panel::NoSound;
  if (!session.calibration)
    return Panel::Silence;

  switch (session.calibration->step()) {
  case onset::CalibrationStep::Silence:
    break;
  case onset::CalibrationStep::Notes:
    return Panel::Notes;
  case onset::CalibrationStep::Done:
    return Panel::Result;
  }
  return Panel::Silence;
}

std::string statusText() {
  const Trainer &trainerRef = trainer();
  const Status &status = trainerRef.status();
  const Settings &settings = trainerRef.settings();

  if (const auto &session = trainerRef.calibration()) {
    switch (panelOf(*session)) {
    case Panel::Silence:
      return fmt::format("Calibrating {}: step 1 of 2 — silence", session->channelName);
    case Panel::Notes:
      return fmt::format("Calibrating {}: step 2 of 2 — notes", session->channelName);
    case Panel::NoSound:
    case Panel::Result:
      break;
    }
    return "Calibration finished";
  }

  std::string text =
      fmt::format("{}, window {}, latency {:.1f} ms",
                  kModeNames[static_cast<std::size_t>(modeIndex(settings.mode))],
                  rangeText(settings.window()), status.compensationMs);
  if (status.rate != 1.0)
    text += fmt::format(", rate {}", status.rate);

  switch (status.measuring) {
  case Measuring::Waiting:
    text += " — waiting for playback";
    break;
  case Measuring::Running:
    text += " — measuring";
    break;
  case Measuring::NoInput:
    text += " — selected input is not available";
    break;
  case Measuring::NotEnabled:
    text += " — MIDI input is not enabled in REAPER preferences";
    break;
  }
  if (status.calibrationCancelled)
    text += " · calibration cancelled: playback started";
  return text;
}

/// Строка состояния, которая сейчас в окне.
std::string shownStatus;

/// Пишет строку состояния в окно `dialog`, а когда она меняется, — в журнал:
/// по журналу проверки видят её без снимка экрана.
void showStatus(HWND dialog) {
  const std::string text = statusText();
  if (text != shownStatus) {
    journal("window status: {}", text);
    shownStatus = text;
  }
  SetDlgItemText(dialog, IDC_STATUS, text.c_str());
}

/// Пишет окно попадания из настроек в поля Offset и Tolerance, кроме поля
/// `editing`, в котором сейчас пишут (0 — в оба), и диапазоном рядом со
/// шкалой.
void showHitWindow(HWND dialog, int editing = 0) {
  const bool showing = showingSettings;
  showingSettings = true;
  const grid::HitWindow hit = trainer().settings().window();
  if (editing != IDC_OFFSET)
    SetDlgItemText(dialog, IDC_OFFSET,
                   grid::halfMsText(hit.offsetMs, grid::MsStyle::Signed).c_str());
  if (editing != IDC_TOLERANCE)
    SetDlgItemText(dialog, IDC_TOLERANCE,
                   grid::halfMsText(hit.toleranceMs, grid::MsStyle::Plain).c_str());
  SetDlgItemText(dialog, IDC_RANGE, rangeText(hit).c_str());
  showingSettings = showing;
}

void placePanel(HWND dialog);
RECT rowsArea(HWND dialog);

/// Пункт списка Input: канал звуковой карты или вход MIDI.
struct InputChoice {
  /// Канал звуковой карты, 0 — первый; −1 — пункт входа MIDI `midi`.
  int channel = -1;

  /// Вход MIDI пункта; у пункта канала пуст.
  MidiInput midi;
};

/// Пункты списка Input в его порядке: по номеру пункта — канал или вход MIDI.
std::vector<InputChoice> inputChoices;

#ifdef _WIN32
/// Делает раскрытый список `combo` не уже самого длинного пункта `labels`
/// (UTF-8).
void fitDroppedWidth(HWND combo, const std::vector<std::string> &labels) {
  HDC context = GetDC(combo);
  if (!context)
    return;

  // Win32 отдаёт шрифт контрола целым LRESULT: приведения к указателю не
  // избежать.
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  auto *font = reinterpret_cast<HFONT>(SendMessage(combo, WM_GETFONT, 0, 0));
  HGDIOBJ previous = font ? SelectObject(context, font) : nullptr;
  int widest = 0;
  for (const std::string &label : labels) {
    // Модуль собран без UNICODE: ширину текста в UTF-8 меряет W-версия.
    std::wstring wide(label.size(), L'\0');
    const int length =
        MultiByteToWideChar(CP_UTF8, 0, label.data(), static_cast<int>(label.size()),
                            wide.data(), static_cast<int>(wide.size()));
    SIZE size{};
    if (length > 0 && GetTextExtentPoint32W(context, wide.data(), length, &size))
      widest = std::max(widest, static_cast<int>(size.cx));
  }
  if (previous)
    SelectObject(context, previous);
  ReleaseDC(combo, context);

  // Поля пункта и полоса прокрутки списка.
  SendMessage(combo, CB_SETDROPPEDWIDTH,
              static_cast<WPARAM>(widest + GetSystemMetrics(SM_CXVSCROLL) + 8), 0);
}
#endif

/// Заполняет список Input каналами звуковой карты, затем входами MIDI
/// (`midiInputs`), выбирает в нём вход настроек и пишет пункты в журнал.
/// Выбранный вход MIDI, которого сейчас нет, оставляет список без выбора, как
/// пропавший канал.
void fillInputs(HWND dialog) {
  const bool showing = showingSettings;
  showingSettings = true;
  const Settings &settings = trainer().settings();
  const std::optional<MidiInput> &chosen = trainer().midiInput();

  inputChoices.clear();
  std::vector<std::string> labels;
  int selected = -1;

  const int channels = GetNumAudioInputs();
  for (int i = 0; i < channels; ++i) {
    const char *name = GetInputChannelName(i);
    if (settings.midiInput.empty() && settings.channel == i)
      selected = static_cast<int>(labels.size());
    labels.push_back(fmt::format("{}: {}", i + 1, name ? name : ""));
    inputChoices.push_back({.channel = i, .midi = {}});
  }
  for (MidiInput &input : midiInputs()) {
    if (!settings.midiInput.empty() && chosen && chosen->device == input.device)
      selected = static_cast<int>(labels.size());
    labels.push_back(
        fmt::format("MIDI: {}{}", input.name, input.enabled ? "" : " (not enabled)"));
    inputChoices.push_back({.channel = -1, .midi = std::move(input)});
  }

  HWND combo = GetDlgItem(dialog, IDC_CHANNEL);
  SendMessage(combo, CB_RESETCONTENT, 0, 0);
  std::string listed;
  for (const std::string &label : labels) {
    SendMessage(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    listed += fmt::format("{}«{}»", listed.empty() ? "" : ", ", label);
  }
  SendMessage(combo, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
#ifdef _WIN32
  fitDroppedWidth(combo, labels);
#endif
  journal("input list: {}; selected {}", listed, selected);
  showingSettings = showing;
}

void fillControls(HWND dialog) {
  showingSettings = true;
  const Settings &settings = trainer().settings();

  SendDlgItemMessage(dialog, IDC_MODE, CB_SETCURSEL,
                     static_cast<WPARAM>(modeIndex(settings.mode)), 0);
  showHitWindow(dialog);
  SetDlgItemText(dialog, IDC_SILENCE,
                 fmt::format("{}", static_cast<int>(settings.silenceDb)).c_str());
  CheckDlgButton(dialog, IDC_SHOW_NUMBERS,
                 settings.showBarNumbers ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(dialog, IDC_SHOW_STATS, settings.showBarStats ? BST_CHECKED : BST_UNCHECKED);

  // Входы — заново при каждом показе: звуковую карту могли сменить, а
  // устройство MIDI — подключить.
  fillInputs(dialog);

  showStatus(dialog);
  placePanel(dialog);
  showingSettings = false;
}

/// Делает то, что велит кнопка калибровки `control`.
///
/// @return ложь, если `control` — не кнопка калибровки.
bool onCalibrationButton(int control) {
  switch (control) {
  case IDC_CALIBRATE:
    // На итоге кнопка называется Finish и закрывает панель.
    if (trainer().calibrationFinished())
      trainer().endCalibration();
    else
      trainer().startCalibration();
    break;
  case IDC_CAL_AGAIN:
    trainer().startCalibration();
    break;
  case IDC_CAL_CANCEL:
  case IDC_CAL_CLOSE:
    trainer().endCalibration();
    break;
  case IDC_CAL_APPLY:
    trainer().applyCalibration();
    break;
  default:
    return false;
  }
  refreshWindow();
  return true;
}

/// Ставит в настройки `settings` вход, выбранный в списке Input окна
/// `dialog`.
///
/// @return ложь, если в списке ничего не выбрано: настройки не меняются.
bool chooseInput(HWND dialog, Settings &settings) {
  const auto index = SendDlgItemMessage(dialog, IDC_CHANNEL, CB_GETCURSEL, 0, 0);
  if (index < 0 || index >= static_cast<LRESULT>(inputChoices.size()))
    return false;

  const InputChoice &choice = inputChoices[static_cast<std::size_t>(index)];
  if (choice.channel >= 0) {
    settings.channel = choice.channel;
    settings.midiInput.clear();
    settings.midiIndex = -1;
  } else {
    settings.midiInput = choice.midi.key;
    settings.midiIndex = choice.midi.device;
  }
  return true;
}

/// Окно попадания настроек `settings` после правки поля `control` — Offset
/// или Tolerance: вписанное значение прижато к шкале.
///
/// @return пусто, если в поле недописанное число.
std::optional<grid::HitWindow> editedWindow(HWND dialog, int control,
                                            const Settings &settings) {
  const auto value = halfEditValue(dialog, control);
  if (!value)
    return std::nullopt;
  return control == IDC_OFFSET
             ? grid::withOffset(settings.window(), *value, settings.scaleMs)
             : grid::withTolerance(settings.window(), *value, settings.scaleMs);
}

/// Настройки после правки контрола `control` с уведомлением `notification`.
///
/// @return пусто, если уведомление настроек не меняет: контрол не настройка,
///   в поле недописанное число или в списке ничего не выбрано.
std::optional<Settings> editedSettings(HWND dialog, int control, int notification) {
  Settings settings = trainer().settings();
  if (notification == CBN_SELCHANGE && control == IDC_MODE) {
    const auto index = SendDlgItemMessage(dialog, IDC_MODE, CB_GETCURSEL, 0, 0);
    if (index < 0 || index >= static_cast<LRESULT>(kModes.size()))
      return std::nullopt;
    settings.mode = kModes[static_cast<std::size_t>(index)];
  } else if (notification == CBN_SELCHANGE && control == IDC_CHANNEL) {
    if (!chooseInput(dialog, settings))
      return std::nullopt;
  } else if (notification == EN_CHANGE &&
             (control == IDC_OFFSET || control == IDC_TOLERANCE)) {
    // Недописанное число настройки не трогает; вне границ — прижимается.
    const auto hit = editedWindow(dialog, control, settings);
    if (!hit)
      return std::nullopt;
    setWindow(settings, *hit);
  } else if (notification == BN_CLICKED && control == IDC_SETTINGS_TOGGLE) {
    settings.panelCollapsed = !settings.panelCollapsed;
  } else if (notification == EN_CHANGE && control == IDC_SILENCE) {
    const auto value = editValue(dialog, IDC_SILENCE);
    if (!value)
      return std::nullopt;
    settings.silenceDb = *value;
  } else if (notification == BN_CLICKED && control == IDC_SHOW_NUMBERS) {
    settings.showBarNumbers = IsDlgButtonChecked(dialog, IDC_SHOW_NUMBERS) == BST_CHECKED;
  } else if (notification == BN_CLICKED && control == IDC_SHOW_STATS) {
    settings.showBarStats = IsDlgButtonChecked(dialog, IDC_SHOW_STATS) == BST_CHECKED;
  } else {
    return std::nullopt;
  }
  return settings;
}

void onCommand(HWND dialog, int control, int notification) {
  if (showingSettings)
    return;
  if (notification == BN_CLICKED && onCalibrationButton(control))
    return;
  if (notification == BN_CLICKED && control == IDC_INPUT_REFRESH) {
    // Устройство MIDI могли подключить или включить в настройках REAPER.
    trainer().lookupMidiInput();
    fillInputs(dialog);
    return;
  }
  if (notification == EN_KILLFOCUS && (control == IDC_OFFSET || control == IDC_TOLERANCE)) {
    // Ушли из поля — в нём прижатое значение: видно, что вписанное прижалось.
    showHitWindow(dialog);
    return;
  }

  const std::optional<Settings> settings = editedSettings(dialog, control, notification);
  if (!settings)
    return;
  trainer().setSettings(*settings);
  journalSettings("settings");
  if (control == IDC_OFFSET || control == IDC_TOLERANCE)
    showHitWindow(dialog, control);
  refreshWindow();
  if (control == IDC_SETTINGS_TOGGLE)
    journal("panel {}: rows from {} px", settings->panelCollapsed ? "collapsed" : "expanded",
            rowsArea(dialog).top);
}

/// Цвета окна на тёмном фоне (design.md D8).
constexpr int kBackground = RGB(24, 24, 24);
constexpr int kCurrentRow = RGB(36, 36, 36);
constexpr int kCurrentBeatBack = RGB(69, 59, 40);
constexpr int kCurrentBeat = RGB(255, 190, 60);
constexpr int kChange = RGB(110, 170, 255);
constexpr int kLine = RGB(56, 56, 56);
constexpr int kNeutral = RGB(170, 170, 170);

/// Строк тактов в окне.
constexpr int kRows = 8;

/// Наименьший кегль, пикселей: мельче цифры не читаются.
constexpr double kMinFont = 9.0;

/// Цифры числа на подложке попадания ровно в смещение.
constexpr int kPlateText = RGB(20, 20, 20);

int colorOf(grid::Tone tone) {
  switch (tone) {
  case grid::Tone::Good:
  case grid::Tone::Target:
    return RGB(90, 210, 90);
  case grid::Tone::Bad:
    return RGB(235, 80, 70);
  case grid::Tone::Neutral:
    break;
  }
  return kNeutral;
}

/// Округляет координату до пикселя.
int pixel(double value) { return static_cast<int>(std::lround(value)); }

void fillBox(HDC context, double left, double top, double right, double bottom, int color) {
  const RECT box{
      .left = pixel(left), .top = pixel(top), .right = pixel(right), .bottom = pixel(bottom)};
  HBRUSH brush = CreateSolidBrush(color);
  FillRect(context, &box, brush);
  DeleteObject(brush);
}

/// Круглая точка диаметром `diameter` с центром в (`x`, `y`).
void drawDot(HDC context, double x, double y, double diameter, int color) {
  HBRUSH brush = CreateSolidBrush(color);
  HPEN pen = CreatePen(PS_SOLID, 0, color);
  HGDIOBJ previousBrush = SelectObject(context, brush);
  HGDIOBJ previousPen = SelectObject(context, pen);
  const double radius = diameter / 2.0;
  Ellipse(context, pixel(x - radius), pixel(y - radius), pixel(x + radius), pixel(y + radius));
  SelectObject(context, previousPen);
  SelectObject(context, previousBrush);
  DeleteObject(pen);
  DeleteObject(brush);
}

double textWidth(HDC context, const std::string &text);

/// Скруглённый прямоугольник цвета `color` от (`left`, `top`) до (`right`,
/// `bottom`) с радиусом углов `radius`.
void fillRounded(HDC context, double left, double top, double right, double bottom,
                 double radius, int color) {
  HBRUSH brush = CreateSolidBrush(color);
  HPEN pen = CreatePen(PS_SOLID, 0, color);
  HGDIOBJ previousBrush = SelectObject(context, brush);
  HGDIOBJ previousPen = SelectObject(context, pen);
  RoundRect(context, pixel(left), pixel(top), pixel(right), pixel(bottom), pixel(radius * 2.0),
            pixel(radius * 2.0));
  SelectObject(context, previousPen);
  SelectObject(context, previousBrush);
  DeleteObject(pen);
  DeleteObject(brush);
}

/// Текст с центром в (`x`, `y`); вылезать за поле `halfWidth` по сторонам ему
/// можно.
void drawCentered(HDC context, const std::string &text, int color, double x, double y,
                  double halfWidth, double halfHeight) {
  RECT box{.left = pixel(x - halfWidth),
           .top = pixel(y - halfHeight),
           .right = pixel(x + halfWidth),
           .bottom = pixel(y + halfHeight)};
  SetTextColor(context, color);
  DrawTextUTF8(context, text.c_str(), -1, &box,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
}

/// Шрифты Arial одного рисования по кеглю и жирности: создаются при первом
/// выборе, удаляются вместе с объектом, прежний шрифт контекста
/// возвращается.
class Fonts {
public:
  explicit Fonts(HDC context) : context_(context) {}

  Fonts(const Fonts &) = delete;
  Fonts &operator=(const Fonts &) = delete;
  Fonts(Fonts &&) = delete;
  Fonts &operator=(Fonts &&) = delete;

  ~Fonts() {
    if (previous_)
      SelectObject(context_, previous_);
    for (const Made &made : fonts_)
      DeleteObject(made.font);
  }

  /// Выбирает в контекст шрифт кегля `size`, пикселей, — высоты цифр, как
  /// `font-size` макета; жирный, если `bold`.
  void use(double size, bool bold = true) {
    // Высота ячейки шрифта больше высоты знаков: у Arial — примерно в 1.15 раза.
    const int height = pixel(size * 1.15);
    HFONT font = nullptr;
    for (const Made &made : fonts_)
      if (made.height == height && made.bold == bold)
        font = made.font;
    if (!font) {
      font = CreateFont(height, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, 0, 0, 0, 0, 0, 0, 0, 0,
                        "Arial");
      fonts_.push_back({.height = height, .bold = bold, .font = font});
    }
    HGDIOBJ previous = SelectObject(context_, font);
    if (!previous_)
      previous_ = previous;
  }

private:
  struct Made {
    int height = 0;
    bool bold = true;
    HFONT font = nullptr;
  };

  HDC context_;
  HGDIOBJ previous_ = nullptr;
  std::vector<Made> fonts_;
};

/// Раскладка области строк по горизонтали и общие кегли (design.md D8).
struct Layout {
  /// Левый край области строк и её ширина, пикселей.
  double left = 0.0;
  double width = 0.0;

  /// Высота строки такта: область строк на восемь.
  double rowHeight = 0.0;

  /// Колонка номеров тактов слева и колонка среднего и разброса справа;
  /// выключенная колонка — ноль.
  double labelWidth = 0.0;
  double statsWidth = 0.0;

  /// Шкала такта — всё остальное.
  double scaleWidth = 0.0;

  /// Кегль номера такта, среднего и разброса и кегль маркера смены.
  double sideSize = 0.0;
  double statsSize = 0.0;
  double markSize = 0.0;
};

/// Раскладка области строк шириной `width` с левым краем `left` и высотой
/// `height` при настройках колонок `settings`.
Layout layoutOf(double left, double width, double height, const Settings &settings) {
  Layout layout{.left = left, .width = width, .rowHeight = height / kRows};
  // Колонка номера — под пять цифр, до такта 10000; колонка среднего и
  // разброса — около 9 % ширины.
  layout.sideSize = std::max(kMinFont, layout.rowHeight * 0.34);
  layout.labelWidth = settings.showBarNumbers ? layout.sideSize * 0.6 * 5.0 + 10.0 : 0.0;
  layout.statsWidth = settings.showBarStats ? width * 0.09 : 0.0;
  layout.statsSize = std::max(kMinFont, std::min(layout.sideSize, layout.statsWidth / 6.0));
  layout.scaleWidth = width - layout.labelWidth - layout.statsWidth;
  layout.markSize = std::max(kMinFont, layout.rowHeight * 0.2);
  return layout;
}

/// Рисует строку такта `row`, верх которой — `top`: номер такта, шкалу по
/// ударам, точки, значения, текущий удар, маркеры смен, среднее и разброс.
void paintRow(HDC context, Fonts &fonts, const Layout &layout, const grid::BarView &row,
              double top) {
  const double middle = top + layout.rowHeight / 2.0;
  const double beats = std::max(row.beats, 1);
  const double division = std::max(row.division, 1);

  // Крупное число — по высоте строки, ширине удара и расстоянию между узлами
  // самого мелкого режима такта; мелкое — 0.6 крупного.
  const auto bigFor = [&](double step) {
    return std::max(kMinFont,
                    std::min({layout.rowHeight * 0.62, step / 2.3, step / division / 1.55}));
  };
  // Перед первым ударом — место под половину крупного числа.
  const double pad = bigFor(layout.scaleWidth / beats) * 0.95 + 6.0;
  const double step = (layout.scaleWidth - pad) / beats;
  const double bigSize = bigFor(step);
  const double smallSize =
      std::max(kMinFont - 1.0, std::min(bigSize * 0.6, step / division / 2.0));
  const double scaleLeft = layout.left + layout.labelWidth;
  const auto x = [&](double beat) { return scaleLeft + pad + step * beat; };

  if (row.currentBeat) {
    fillBox(context, layout.left, top, layout.left + layout.width, top + layout.rowHeight,
            kCurrentRow);
    const double beat = *row.currentBeat;
    fillBox(context, std::max(scaleLeft + 1.0, x(beat) - step * 0.125), top,
            x(beat) + step * 0.875, top + layout.rowHeight, kCurrentBeatBack);
  }

  // Маркер смены — флажок над строкой чисел: древко над ударом смены,
  // подпись справа от него.
  fonts.use(layout.markSize);
  SetTextColor(context, kChange);
  for (const grid::MarkView &mark : row.marks) {
    const double at = x(mark.beat);
    fillBox(context, at - 1.0, top + 2.0, at + 1.0, top + 4.0 + layout.markSize, kChange);
    RECT box{.left = pixel(at + 4.0),
             .top = pixel(top + 2.0),
             .right = pixel(at + 4.0 + layout.scaleWidth),
             .bottom = pixel(top + 4.0 + layout.markSize * 1.2)};
    DrawTextUTF8(context, mark.text.c_str(), -1, &box,
                 DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOCLIP);
  }

  if (layout.labelWidth > 0.0) {
    fonts.use(layout.sideSize);
    drawCentered(context, row.label, kNeutral, layout.left + layout.labelWidth / 2.0, middle,
                 layout.labelWidth / 2.0, layout.rowHeight / 2.0);
  }

  for (const int beat : row.dots) {
    const bool current = row.currentBeat == beat;
    drawDot(context, x(beat), middle, std::max(4.0, bigSize * (current ? 0.3 : 0.2)),
            current ? kCurrentBeat : kNeutral);
  }

  for (const grid::ValueView &value : row.values) {
    const double size = value.onBeat ? bigSize : smallSize;
    fonts.use(size);
    int color = colorOf(value.cell.tone);
    if (value.cell.tone == grid::Tone::Target) {
      // Подложка — по mockup.html: поля 0.2 кегля по сторонам и 0.08 сверху и
      // снизу, скругление 0.18 кегля.
      const double halfWidth = textWidth(context, value.cell.text) / 2.0 + size * 0.2;
      const double halfHeight = size * 0.58;
      fillRounded(context, x(value.beat) - halfWidth, middle - halfHeight,
                  x(value.beat) + halfWidth, middle + halfHeight, size * 0.18, color);
      color = kPlateText;
    }
    drawCentered(context, value.cell.text, color, x(value.beat), middle, step,
                 layout.rowHeight / 2.0);
  }

  if (layout.statsWidth > 0.0 && row.mean && row.spread) {
    fonts.use(layout.statsSize);
    const double right = layout.left + layout.width;
    drawCentered(context, row.mean->text, colorOf(row.mean->tone),
                 right - layout.statsWidth * 0.72, middle, layout.statsWidth * 0.28,
                 layout.rowHeight / 2.0);
    drawCentered(context, row.spread->text, colorOf(row.spread->tone),
                 right - layout.statsWidth * 0.25, middle, layout.statsWidth * 0.25,
                 layout.rowHeight / 2.0);
  }
}

/// Цвета панели калибровки, которых нет у строк (design D7, mockup.html).
constexpr int kGood = RGB(90, 210, 90);
constexpr int kBad = RGB(235, 80, 70);
constexpr int kDim = RGB(125, 125, 125);
constexpr int kText = RGB(221, 221, 221);
constexpr int kBright = RGB(255, 255, 255);
constexpr int kSlotBorder = RGB(60, 110, 60);
constexpr int kTrack = RGB(40, 40, 40);
constexpr int kNoise = RGB(62, 62, 62);
constexpr int kNoiseStripe = RGB(100, 100, 100);
constexpr int kLive = RGB(115, 115, 115);
constexpr int kBand = RGB(45, 95, 45);

/// Кнопки панели калибровки, пикселей.
constexpr int kButtonHeight = 24;
constexpr int kButtonGap = 8;

/// Знак минуса — тот же, что у отклонений.
constexpr std::string_view kMinus = "−";

/// Уровень в dBFS для панели: `decimals` знаков после запятой, минус — тот
/// же, что у отклонений; «−0» не бывает.
std::string dbText(double value, int decimals = 0) {
  std::string text = fmt::format("{:.{}f}", std::abs(value), decimals);
  const bool zero = std::ranges::all_of(text, [](char c) { return c == '0' || c == '.'; });
  if (value < 0.0 && !zero)
    text.insert(0, kMinus);
  return text;
}

/// Ширина строки шрифтом, выбранным в контекст, пикселей.
double textWidth(HDC context, const std::string &text) {
  RECT box{.left = 0, .top = 0, .right = 0, .bottom = 0};
  DrawTextUTF8(context, text.c_str(), -1, &box,
               DT_LEFT | DT_TOP | DT_SINGLELINE | DT_CALCRECT);
  return static_cast<double>(box.right - box.left);
}

/// Пишет строку, левый верхний угол — (`x`, `y`).
void drawLeft(HDC context, const std::string &text, int color, double x, double y) {
  RECT box{.left = pixel(x), .top = pixel(y), .right = pixel(x) + 1, .bottom = pixel(y) + 1};
  SetTextColor(context, color);
  DrawTextUTF8(context, text.c_str(), -1, &box, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOCLIP);
}

/// Пишет текст шрифтом, выбранным в контекст, с переносом по пробелам в
/// полосе от `left` до `right`, верх — `top`, шаг строк — `lineHeight`.
/// Слово шире полосы стоит в строке одно и вылезает за её край.
///
/// @return низ написанного текста.
double drawWrapped(HDC context, const std::string &text, int color, double left, double top,
                   double right, double lineHeight) {
  // Переносит сам: DT_WORDBREAK в SWELL на Linux строку не переносит.
  double y = top;
  std::string line;
  std::size_t from = 0;
  while (from <= text.size()) {
    const std::size_t space = std::min(text.find(' ', from), text.size());
    const std::string word = text.substr(from, space - from);
    std::string longer = line;
    if (!longer.empty())
      longer += ' ';
    longer += word;
    if (!line.empty() && textWidth(context, longer) > right - left) {
      drawLeft(context, line, color, left, y);
      y += lineHeight;
      line = word;
    } else {
      line = longer;
    }
    from = space + 1;
  }
  if (!line.empty()) {
    drawLeft(context, line, color, left, y);
    y += lineHeight;
  }
  return y;
}

/// Рамка толщиной в пиксель.
void drawFrame(HDC context, double left, double top, double right, double bottom, int color) {
  fillBox(context, left, top, right, top + 1.0, color);
  fillBox(context, left, bottom - 1.0, right, bottom, color);
  fillBox(context, left, top, left + 1.0, bottom, color);
  fillBox(context, right - 1.0, top, right, bottom, color);
}

/// Что отмечено на шкале уровней; пустое не рисуется. Уровни — dBFS.
struct ScaleMarks {
  std::optional<double> level = std::nullopt;
  std::optional<double> noise = std::nullopt;
  std::vector<double> attacks = {};
  std::optional<double> peak = std::nullopt;
  std::optional<double> oldThreshold = std::nullopt;
  std::optional<double> newThreshold = std::nullopt;
};

/// Рисует шкалу уровней −100…0 dBFS в полосе от `left` до `right`, от `top`
/// высотой `height`: дорожку с текущим уровнем, шумом, атаками, полосой атак,
/// пиком и порогами, подписи отметок и деления.
void paintScale(HDC context, Fonts &fonts, double left, double right, double top,
                double height, const ScaleMarks &marks) {
  const auto x = [&](double db) {
    return left + (std::clamp(db, -100.0, 0.0) + 100.0) / 100.0 * (right - left);
  };
  const double trackTop = top + height * 0.42;
  const double trackBottom = top + height * 0.60;
  const double tickTop = top + height * 0.34;
  const double tickBottom = top + height * 0.68;

  fillBox(context, left, trackTop, right, trackBottom, kTrack);
  if (marks.noise) {
    const double edge = x(*marks.noise + 3.0);
    fillBox(context, left, trackTop, edge, trackBottom, kNoise);
    // Штриховка: полоска в 2 пикселя через каждые 6.
    for (int stripe = 0; left + 6.0 * stripe + 5.0 < edge; ++stripe)
      fillBox(context, left + 6.0 * stripe + 3.0, trackTop, left + 6.0 * stripe + 5.0,
              trackBottom, kNoiseStripe);
  }
  if (marks.level)
    fillBox(context, left, trackTop + 2.0, x(*marks.level), trackBottom - 2.0, kLive);
  if (!marks.attacks.empty() && marks.peak) {
    const auto [softest, loudest] = std::ranges::minmax(marks.attacks);
    fillBox(context, x(softest), trackTop, x(loudest), trackBottom, kBand);
  }
  drawFrame(context, left, trackTop, right, trackBottom, kLine);

  for (const double attack : marks.attacks)
    fillBox(context, x(attack) - 1.0, tickTop, x(attack) + 1.0, tickBottom, kGood);

  const double labelSize = std::max(kMinFont, height * 0.15);
  fonts.use(labelSize, false);
  // Подпись по центру `at`, не вылезая за края шкалы.
  const auto label = [&](const std::string &text, int color, double at, double y) {
    const double width = textWidth(context, text);
    drawLeft(context, text, color, std::clamp(at - width / 2.0, left, right - width), y);
  };
  const double above = top;
  const double below = tickBottom + 2.0;

  if (marks.oldThreshold) {
    const double at = x(*marks.oldThreshold);
    // Пунктир: штрих в 4 пикселя через каждые 8.
    const double from = tickTop - height * 0.04;
    const double to = tickBottom + height * 0.04;
    for (int dash = 0; from + 8.0 * dash < to; ++dash)
      fillBox(context, at - 1.0, from + 8.0 * dash, at + 1.0,
              std::min(from + 8.0 * dash + 4.0, to), kDim);
    label(fmt::format("{} {}", marks.newThreshold ? "was" : "Silence",
                      dbText(*marks.oldThreshold)),
          kDim, at, above);
  }
  if (marks.newThreshold) {
    const double at = x(*marks.newThreshold);
    fillBox(context, at - 1.0, tickTop - height * 0.04, at + 1.0, tickBottom + height * 0.04,
            kCurrentBeat);
    label(fmt::format("Silence {}", dbText(*marks.newThreshold)), kCurrentBeat, at, above);
  }
  if (marks.noise)
    label(fmt::format("noise {}", dbText(*marks.noise)), kNeutral,
          (left + x(*marks.noise)) / 2.0, below);
  if (!marks.attacks.empty() && marks.peak) {
    const auto [softest, loudest] = std::ranges::minmax(marks.attacks);
    label(fmt::format("pick {}…{}", dbText(softest), dbText(loudest)), kGood,
          (x(softest) + x(loudest)) / 2.0, below);
  }
  if (marks.peak) {
    fillBox(context, x(*marks.peak) - 1.0, tickTop, x(*marks.peak) + 1.0, tickBottom, kBright);
    label(fmt::format("peak {}", dbText(*marks.peak, 1)), kBright, x(*marks.peak), below);
  }

  fonts.use(std::max(kMinFont, height * 0.125), false);
  for (int db = -100; db <= 0; db += 10)
    label(dbText(db), kDim, x(db), top + height - std::max(kMinFont, height * 0.125) * 1.2);
}

/// Цвета шкалы окна попадания (mockup.html этого изменения).
constexpr int kOutside = RGB(80, 40, 37);
constexpr int kGaugeFrame = RGB(60, 60, 60);
constexpr int kHandle = RGB(228, 228, 228);
constexpr int kHandleFrame = RGB(34, 34, 34);
constexpr int kHandleGrip = RGB(119, 119, 119);

/// Половина ширины ползунка шкалы и расстояние до него, на котором щелчок
/// берёт ползунок, пикселей.
constexpr double kHandleHalf = 4.0;
constexpr double kHandleReach = 6.0;

/// Раскладка шкалы окна попадания в прямоугольнике: дорожка и перевод
/// миллисекунд в пиксели и обратно.
struct GaugeLayout {
  /// Края дорожки по горизонтали и вертикали, пикселей.
  double left = 0.0;
  double right = 0.0;
  double top = 0.0;
  double bottom = 0.0;

  /// На сколько ползунки и отметка клика выходят за дорожку сверху и снизу,
  /// пикселей.
  double reach = 0.0;

  /// Середина строки подписей делений и их кегль, пикселей.
  double labelMiddle = 0.0;
  double labelSize = 0.0;

  /// Граница шкалы, мс: шкала от −`scaleMs` до +`scaleMs`.
  double scaleMs = grid::kDefaultScaleMs;

  /// Место `ms` на дорожке, пикселей; за шкалой — её край.
  double x(double ms) const {
    return left +
           (std::clamp(ms, -scaleMs, scaleMs) + scaleMs) / (2.0 * scaleMs) * (right - left);
  }

  /// Миллисекунды места `at` на дорожке; за дорожкой — дальше края шкалы.
  double ms(double at) const { return (at - left) / (right - left) * 2.0 * scaleMs - scaleMs; }
};

/// Раскладка шкалы с границей `scaleMs` в прямоугольнике `box`: дорожка — в
/// верхней половине, подписи — под ней, по краям — место под половину
/// подписи.
GaugeLayout gaugeLayout(const RECT &box, double scaleMs) {
  const double height = box.bottom - box.top;
  GaugeLayout layout{.top = box.top + height * 0.16,
                     .bottom = box.top + height * 0.56,
                     .reach = height * 0.1,
                     .labelSize = std::max(kMinFont, height * 0.28),
                     .scaleMs = std::max(scaleMs, 1.0)};
  const double margin = layout.labelSize * 1.4;
  layout.left = box.left + margin;
  layout.right = std::max(layout.left + 1.0, box.right - margin);
  layout.labelMiddle = (layout.bottom + layout.reach + box.bottom) / 2.0;
  return layout;
}

/// Шаг подписей делений шкалы, мс: самый частый, при котором подписи не
/// налезают друг на друга.
double tickStep(const GaugeLayout &layout) {
  const double perMs = (layout.right - layout.left) / (2.0 * layout.scaleMs);
  for (const double step : {5.0, 10.0, 20.0, 25.0, 50.0, 100.0})
    if (step * perMs >= layout.labelSize * 3.0)
      return step;
  return 200.0;
}

/// Рисует шкалу окна попадания `hit` с границей `scaleMs` в прямоугольнике
/// `box` (mockup.html): дорожку вне окна, полосу окна, пунктир смещения,
/// отметку клика, ползунки краёв и подписи делений. Фон — цвет фона диалога.
void paintGauge(HDC context, const RECT &box, const grid::HitWindow &hit, double scaleMs) {
  const GaugeLayout layout = gaugeLayout(box, scaleMs);
  fillBox(context, box.left, box.top, box.right, box.bottom, GetSysColor(COLOR_3DFACE));
  fillBox(context, layout.left, layout.top, layout.right, layout.bottom, kOutside);
  fillBox(context, layout.x(hit.early()), layout.top, layout.x(hit.late()), layout.bottom,
          kBand);
  drawFrame(context, layout.left, layout.top, layout.right, layout.bottom, kGaugeFrame);

  // Пунктир смещения: штрих в 2 пикселя через каждые 4.
  const double center = layout.x(hit.offsetMs);
  for (int dash = 0; layout.top + 4.0 * dash + 4.0 <= layout.bottom - 2.0; ++dash) {
    const double y = layout.top + 2.0 + 4.0 * dash;
    fillBox(context, center - 1.0, y, center + 1.0, y + 2.0, kGood);
  }

  const double zero = layout.x(0.0);
  fillBox(context, zero - 1.0, layout.top - layout.reach, zero + 1.0,
          layout.bottom + layout.reach, kBright);

  const double middle = (layout.top + layout.bottom) / 2.0;
  for (const double edge : {hit.early(), hit.late()}) {
    const double at = layout.x(edge);
    const double top = layout.top - layout.reach;
    const double bottom = layout.bottom + layout.reach;
    fillBox(context, at - kHandleHalf, top, at + kHandleHalf, bottom, kHandle);
    drawFrame(context, at - kHandleHalf, top, at + kHandleHalf, bottom, kHandleFrame);
    fillBox(context, at - 2.0, middle - 2.0, at + 2.0, middle - 1.0, kHandleGrip);
    fillBox(context, at - 2.0, middle + 1.0, at + 2.0, middle + 2.0, kHandleGrip);
  }

  Fonts fonts(context);
  SetBkMode(context, TRANSPARENT);
  const double step = tickStep(layout);
  const int text = GetSysColor(COLOR_BTNTEXT);
  const auto ticks = static_cast<int>(std::floor(layout.scaleMs / step));
  for (int tick = -ticks; tick <= ticks; ++tick) {
    const double ms = tick * step;
    fonts.use(layout.labelSize, tick == 0);
    drawCentered(context, grid::halfMsText(ms, grid::MsStyle::Shown), text, layout.x(ms),
                 layout.labelMiddle, layout.labelSize * 2.0, layout.labelSize * 0.7);
  }
}

/// Раскладка панели калибровки в области `area` (mockup.html): края, кегли и
/// места частей — доли высоты и ширины области.
struct PanelLayout {
  double left = 0.0;
  double right = 0.0;
  double top = 0.0;
  double bottom = 0.0;
  double width = 0.0;
  double height = 0.0;

  /// Верх кнопок внизу панели.
  double buttonsTop = 0.0;
};

PanelLayout panelLayout(const RECT &area) {
  const double width = area.right - area.left;
  const double height = area.bottom - area.top;
  const double pad = std::max(8.0, width * 0.022);
  PanelLayout layout{.left = area.left + pad,
                     .right = area.right - pad,
                     .top = area.top + height * 0.03,
                     .bottom = area.bottom - height * 0.03,
                     .width = width,
                     .height = height};
  layout.buttonsTop = area.bottom - height * 0.02 - kButtonHeight;
  return layout;
}

/// Сколько секунд шага нот без нот до подсказки проверить вход.
constexpr double kQuietHintSeconds = 5.0;

/// Номер шага панели для строки шагов: 1 — тишина, 2 — ноты, 3 — итог.
int stepOf(Panel panel) {
  switch (panel) {
  case Panel::Silence:
    return 1;
  case Panel::Notes:
    return 2;
  case Panel::NoSound:
  case Panel::Result:
    break;
  }
  return 3;
}

/// Цвет шага `index` строки шагов, когда идёт шаг `current`: пройденный —
/// зелёный, текущий — жёлтый, будущий — тусклый.
int stepColor(int index, int current) {
  if (index < current)
    return kGood;
  if (index == current)
    return kCurrentBeat;
  return kDim;
}

/// Рисует панель калибровки сессии `session` в области `area` вместо строк
/// тактов (design D7, mockup.html): строку шагов, крупную подсказку,
/// пояснение и то, что показывает шаг или итог.
class PanelPainter {
public:
  PanelPainter(HDC context, const RECT &area, const CalibrationSession &session)
      : context_(context), fonts_(context), layout_(panelLayout(area)), session_(session),
        silence_(trainer().settings().silenceDb),
        headSize_(std::max(kMinFont, layout_.height * 0.045)),
        saySize_(std::max(kMinFont + 4.0, layout_.height * 0.095)),
        hintSize_(std::max(kMinFont, layout_.height * 0.046)),
        scaleHeight_(std::max(70.0, layout_.height * 0.24)) {
    fillBox(context, area.left, area.top, area.right, area.bottom, kBackground);
    SetBkMode(context, TRANSPARENT);
  }

  void paint() {
    const Panel panel = panelOf(session_);
    steps(stepOf(panel));
    const onset::Calibration *calibration =
        session_.calibration ? &*session_.calibration : nullptr;

    switch (panel) {
    case Panel::Silence:
      silenceStep(calibration);
      return;
    case Panel::Notes:
      if (calibration)
        notesStep(*calibration);
      return;
    case Panel::NoSound:
      noSound();
      return;
    case Panel::Result:
      break;
    }
    if (!calibration)
      return;
    if (const std::optional<onset::CalibrationResult> &done = calibration->result())
      result(*calibration, *done);
  }

private:
  /// Строка шагов: «1. Silence — 2. Notes — Result».
  void steps(int current) {
    fonts_.use(headSize_);
    double x = layout_.left;
    const std::array<const char *, 3> names{"1. Silence", "2. Notes", "Result"};
    for (std::size_t i = 0; i < names.size(); ++i) {
      drawLeft(context_, names[i], stepColor(static_cast<int>(i) + 1, current), x,
               layout_.top);
      x += textWidth(context_, names[i]) + headSize_ * 0.6;
      if (i + 1 < names.size()) {
        const double y = layout_.top + headSize_ * 0.6;
        fillBox(context_, x, y, x + layout_.width * 0.03, y + 1.0, kLine);
        x += layout_.width * 0.03 + headSize_ * 0.6;
      }
    }
  }

  double sayTop() const { return layout_.top + headSize_ * 1.2 + layout_.height * 0.04; }

  /// Крупная подсказка под строкой шагов.
  void say(const std::string &text, int color) {
    fonts_.use(saySize_);
    drawLeft(context_, text, color, layout_.left, sayTop());
  }

  /// Пояснение под подсказкой с переносом по словам.
  ///
  /// @return низ пояснения.
  double hint(const std::string &text, int color) {
    fonts_.use(hintSize_, false);
    return drawWrapped(context_, text, color, layout_.left, sayTop() + saySize_ * 1.3,
                       layout_.right, hintSize_ * 1.3);
  }

  /// Шкала уровней низом на `bottom`, во всю ширину панели с отступами.
  void scale(double bottom, const ScaleMarks &marks) {
    paintScale(context_, fonts_, layout_.left + layout_.width * 0.025,
               layout_.right - layout_.width * 0.025, bottom - scaleHeight_, scaleHeight_,
               marks);
  }

  std::string stays() const {
    return fmt::format(" Silence threshold stays {} dBFS.", dbText(silence_));
  }

  void silenceStep(const onset::Calibration *calibration) {
    say("Mute the strings and don't play", kBright);
    const double left =
        calibration ? calibration->silenceLeft() : onset::kCalibrationSilenceSeconds;
    const std::optional<double> sound =
        calibration ? calibration->secondsSinceSound() : std::nullopt;
    if (sound && *sound < 2.0)
      hint("Heard a sound — starting over. Rest your palm on the strings.", kCurrentBeat);
    else
      hint(fmt::format("Listening to the background noise… {} s", std::ceil(left)), kNeutral);

    // Полоса хода шага над шкалой.
    const double barHeight = std::max(3.0, layout_.height * 0.012);
    const double barTop = layout_.bottom - scaleHeight_ - layout_.height * 0.03 - barHeight;
    const double done = 1.0 - left / onset::kCalibrationSilenceSeconds;
    fillBox(context_, layout_.left, barTop, layout_.right, barTop + barHeight, kLine);
    fillBox(context_, layout_.left, barTop,
            layout_.left + (layout_.right - layout_.left) * done, barTop + barHeight,
            kCurrentBeat);

    ScaleMarks marks{.oldThreshold = silence_};
    if (calibration)
      marks.level = calibration->levelDb();
    scale(layout_.bottom, marks);
  }

  void notesStep(const onset::Calibration &calibration) {
    say(fmt::format("Play {} single notes", onset::kCalibrationNotes), kBright);
    const std::vector<double> &attacks = calibration.attacksDb();
    const double waited = calibration.secondsWithoutNotes();
    const double hintBottom =
        attacks.empty() && waited > kQuietHintSeconds
            ? hint(fmt::format("No notes heard on {}. Is the guitar connected to this input? "
                               "Stops in {} s.",
                               session_.channelName,
                               std::ceil(onset::kCalibrationNotesTimeoutSeconds - waited)),
                   kCurrentBeat)
            : hint("One at a time, muting each. Play as you practice; make two or three of "
                   "them softer.",
                   kNeutral);
    slots(attacks, hintBottom + layout_.height * 0.04);
    scale(layout_.bottom, ScaleMarks{.level = calibration.levelDb(),
                                     .noise = calibration.noiseDb(),
                                     .attacks = attacks,
                                     .oldThreshold = silence_});
  }

  /// Ячейки нот с верхом на `top`: уровень атаки найденной ноты, самая тихая
  /// — в яркой рамке; дальше — сколько нот из скольких.
  void slots(const std::vector<double> &attacks, double top) {
    const double height = layout_.height * 0.13;
    const double width = layout_.width * 0.085;
    const double gap = layout_.width * 0.012;
    const double softest = attacks.empty() ? 0.0 : std::ranges::min(attacks);
    for (int i = 0; i < onset::kCalibrationNotes; ++i) {
      const double left = layout_.left + i * (width + gap);
      const auto index = static_cast<std::size_t>(i);
      if (index >= attacks.size()) {
        drawFrame(context_, left, top, left + width, top + height, kLine);
        // Центр — в целом пикселе: иначе точки разных ячеек выходят разными.
        drawDot(context_, std::round(left + width / 2.0), std::round(top + height / 2.0),
                std::round(std::max(4.0, height * 0.07)), kNeutral);
        continue;
      }
      drawFrame(context_, left, top, left + width, top + height,
                attacks[index] == softest ? kGood : kSlotBorder);
      fonts_.use(std::max(kMinFont, layout_.height * 0.075));
      drawCentered(context_, dbText(attacks[index]), kGood, left + width / 2.0,
                   top + height / 2.0, width / 2.0, height / 2.0);
    }
    fonts_.use(std::max(kMinFont, layout_.height * 0.055));
    drawLeft(context_, fmt::format("{} of {}", attacks.size(), onset::kCalibrationNotes),
             kNeutral, layout_.left + onset::kCalibrationNotes * (width + gap),
             top + height / 2.0 - layout_.height * 0.03);
  }

  void noSound() {
    say("No sound from the audio device", kBad);
    hint("Nothing came from the audio device for 2 s. Check the audio device in Preferences → "
         "Audio → Device, then calibrate again." +
             stays(),
         kNeutral);
  }

  void result(const onset::Calibration &calibration, const onset::CalibrationResult &result) {
    if (result.verdict == onset::Verdict::NotEnoughNotes) {
      notEnoughNotes(result);
      return;
    }

    const double hintBottom = verdict(result);
    table(result, hintBottom + layout_.height * 0.03);
    ScaleMarks marks{.noise = result.noiseDb,
                     .attacks = calibration.attacksDb(),
                     .peak = result.peakDb,
                     .oldThreshold = session_.previousSilenceDb};
    if (result.verdict == onset::Verdict::Good)
      marks.newThreshold = silence_;
    scale(layout_.buttonsTop - 6.0, marks);
  }

  void notEnoughNotes(const onset::CalibrationResult &result) {
    say(result.notes == 0 ? fmt::format("No notes heard on {}", session_.channelName)
                          : fmt::format("Heard only {} of {} notes on {}", result.notes,
                                        onset::kCalibrationNotes, session_.channelName),
        kBad);
    hint(fmt::format("No new note for {} s. Check that the guitar is connected to the input "
                     "selected in Input and that its volume is up, then calibrate again.",
                     onset::kCalibrationNotesTimeoutSeconds) +
             stays(),
         kNeutral);
  }

  /// Заголовок и пояснение итога с нотами.
  ///
  /// @return низ пояснения.
  double verdict(const onset::CalibrationResult &result) {
    const double gap = std::round(result.softestDb - result.noiseDb);
    switch (result.verdict) {
    case onset::Verdict::Good:
      say(fmt::format("Silence threshold: {} → {} dBFS", dbText(session_.previousSilenceDb),
                      dbText(silence_)),
          kGood);
      return hint(
          fmt::format("Set halfway between the noise and the softest pick — {} dB apart.",
                      gap),
          kNeutral);
    case onset::Verdict::Clipping:
      say("The input clips", kBad);
      return hint(fmt::format("The loudest note reached {} dBFS. Turn the input gain down on "
                              "the audio interface so the hardest notes stay below {}6 dBFS, "
                              "then calibrate again.",
                              dbText(result.peakDb, 1), kMinus) +
                      stays(),
                  kNeutral);
    case onset::Verdict::NoiseTooClose:
    case onset::Verdict::NotEnoughNotes:
      break;
    }
    say("Noise is too close to the pick", kBad);
    return hint(fmt::format("Only {} dB between the noise and the softest pick: notes will be "
                            "missed or noise will count as notes. Raise the input gain, turn "
                            "the guitar volume up, or move away from what hums, then "
                            "calibrate again.",
                            gap) +
                    stays(),
                kNeutral);
  }

  /// Таблица уровней итога с верхом на `top`.
  void table(const onset::CalibrationResult &result, double top) {
    const double size = std::max(kMinFont, layout_.height * 0.046);
    const std::array<std::pair<std::string, std::string>, 3> rows{
        std::pair{std::string("Noise above 1 kHz"), dbText(result.noiseDb) + " dBFS"},
        std::pair{
            std::string("Pick attack above 1 kHz"),
            fmt::format("{} … {} dBFS", dbText(result.softestDb), dbText(result.loudestDb))},
        std::pair{std::string("Signal peak"), dbText(result.peakDb, 1) + " dBFS"}};
    fonts_.use(size, false);
    double labelWidth = 0.0;
    for (const auto &row : rows)
      labelWidth = std::max(labelWidth, textWidth(context_, row.first));
    for (std::size_t i = 0; i < rows.size(); ++i) {
      const double y = top + static_cast<double>(i) * size * 1.35;
      fonts_.use(size, false);
      drawLeft(context_, rows[i].first, kNeutral, layout_.left, y);
      fonts_.use(size);
      drawLeft(context_, rows[i].second, kText,
               layout_.left + labelWidth + layout_.width * 0.03, y);
    }
  }

  HDC context_;
  Fonts fonts_;
  PanelLayout layout_;
  const CalibrationSession &session_;
  double silence_;
  double headSize_;
  double saySize_;
  double hintSize_;
  double scaleHeight_;
};

/// Рисует в области `area` контекста строки тактов (design.md D8), а пока идёт
/// калибровка — её панель (design D7). Размеры — доли высоты, так что окно
/// одинаково на любом мониторе.
void paintArea(HDC context, const RECT &area) {
  if (const auto &session = trainer().calibration()) {
    PanelPainter(context, area, *session).paint();
    return;
  }

  fillBox(context, area.left, area.top, area.right, area.bottom, kBackground);

  const Layout layout = layoutOf(area.left, area.right - area.left, area.bottom - area.top,
                                 trainer().settings());
  const std::vector<grid::BarView> rows = grid::present(trainer().bars());

  Fonts fonts(context);
  SetBkMode(context, TRANSPARENT);
  for (std::size_t r = 0; r < rows.size() && r < kRows; ++r)
    paintRow(context, fonts, layout, rows[r],
             static_cast<double>(area.top) + static_cast<double>(r) * layout.rowHeight);

  // Тонкие линии на краях такта: у начала — от номеров тактов, у конца — от
  // среднего и разброса; у выключенной колонки линии нет.
  const double scaleLeft = layout.left + layout.labelWidth;
  if (layout.labelWidth > 0.0)
    fillBox(context, scaleLeft, area.top, scaleLeft + 1.0, area.bottom, kLine);
  if (layout.statsWidth > 0.0)
    fillBox(context, scaleLeft + layout.scaleWidth, area.top,
            scaleLeft + layout.scaleWidth + 1.0, area.bottom, kLine);
}

/// Область строк тактов и панели калибровки: всё под контролами окна. У
/// входа MIDI строки Silence нет, и область — под шкалой окна попадания; у
/// свёрнутой панели настроек — под кнопкой сворачивания.
RECT rowsArea(HWND dialog) {
  RECT client{};
  GetClientRect(dialog, &client);

  const Settings &settings = trainer().settings();
  int lowest = IDC_SILENCE;
  if (settings.panelCollapsed)
    lowest = IDC_SETTINGS_TOGGLE;
  else if (!settings.midiInput.empty())
    lowest = IDC_GAUGE;

  RECT controls{};
  GetWindowRect(GetDlgItem(dialog, lowest), &controls);
  POINT bottom{.x = controls.left, .y = controls.bottom};
  ScreenToClient(dialog, &bottom);

  RECT area = client;
  area.top = bottom.y + 6;
  return area;
}

/// Рисует строки тактов или панель калибровки под контролами окна.
void paintRows(HWND dialog, HDC context) {
  const RECT area = rowsArea(dialog);
  if (area.bottom > area.top)
    paintArea(context, area);
}

/// Прямоугольник шкалы окна попадания в окне — место невидимого контрола
/// `IDC_GAUGE`.
RECT gaugeRect(HWND dialog) {
  RECT box{};
  GetWindowRect(GetDlgItem(dialog, IDC_GAUGE), &box);
  POINT corner{.x = box.left, .y = box.top};
  POINT opposite{.x = box.right, .y = box.bottom};
  ScreenToClient(dialog, &corner);
  ScreenToClient(dialog, &opposite);
  // SWELL на macOS переворачивает ось y: углы берутся по возрастанию.
  return RECT{.left = std::min(corner.x, opposite.x),
              .top = std::min(corner.y, opposite.y),
              .right = std::max(corner.x, opposite.x),
              .bottom = std::max(corner.y, opposite.y)};
}

/// Рисует шкалу окна попадания, если панель настроек развёрнута.
void paintGaugeIn(HWND dialog, HDC context) {
  const Settings &settings = trainer().settings();
  if (!settings.panelCollapsed)
    paintGauge(context, gaugeRect(dialog), settings.window(), settings.scaleMs);
}

/// Что тянут мышью на шкале окна попадания.
enum class Grip {
  /// Ползунок раннего края.
  Early,
  /// Ползунок позднего края.
  Late,
  /// Полоса между ползунками: окно целиком.
  Band,
};

/// Перетаскивание на шкале: что тянут, откуда и окно попадания до него.
struct Drag {
  Grip grip = Grip::Band;
  double fromMs = 0.0;
  grid::HitWindow start;
};

/// Идущее перетаскивание; пусто — мышь на шкале не нажата.
std::optional<Drag> drag;

/// Ползунок под точкой `x` окна, если он ближе `kHandleReach` пикселей;
/// пусто — ни одного.
std::optional<Grip> handleAt(const GaugeLayout &layout, const grid::HitWindow &hit, double x) {
  const double early = std::abs(x - layout.x(hit.early()));
  const double late = std::abs(x - layout.x(hit.late()));
  if (early <= kHandleReach && early <= late)
    return Grip::Early;
  if (late <= kHandleReach)
    return Grip::Late;
  return std::nullopt;
}

/// Ставит окно попадания `hit` в настройки без сохранения и показывает его
/// в полях, на шкале и в строке состояния.
void previewWindow(HWND dialog, const grid::HitWindow &hit) {
  Settings settings = trainer().settings();
  if (settings.window() == hit)
    return;
  setWindow(settings, hit);
  trainer().previewSettings(settings);
  showHitWindow(dialog);
  refreshWindow();
}

/// Двигает то, что тянут, к точке `x` окна.
void dragTo(HWND dialog, double x) {
  if (!drag)
    return;
  const Settings &settings = trainer().settings();
  const GaugeLayout layout = gaugeLayout(gaugeRect(dialog), settings.scaleMs);
  const double ms = layout.ms(x);
  switch (drag->grip) {
  case Grip::Early:
    previewWindow(dialog, grid::withEarly(settings.window(), ms, settings.scaleMs));
    return;
  case Grip::Late:
    previewWindow(dialog, grid::withLate(settings.window(), ms, settings.scaleMs));
    return;
  case Grip::Band:
    break;
  }
  previewWindow(dialog, grid::shifted(drag->start, ms - drag->fromMs, settings.scaleMs));
}

/// Начинает перетаскивание, если точка (`x`, `y`) окна на шкале: ползунок
/// рядом — его край, щелчок в окне попадания — полосу, иначе ближайший
/// ползунок переносится к щелчку. Берёт мышь окну.
///
/// @return ложь, если точка не на шкале или панель свёрнута.
bool startDrag(HWND dialog, int x, int y) {
  const Settings &settings = trainer().settings();
  const RECT box = gaugeRect(dialog);
  if (settings.panelCollapsed || x < box.left || x >= box.right || y < box.top ||
      y >= box.bottom)
    return false;

  const grid::HitWindow hit = settings.window();
  const GaugeLayout layout = gaugeLayout(box, settings.scaleMs);
  const double ms = layout.ms(x);
  Grip grip = Grip::Band;
  if (const auto handle = handleAt(layout, hit, x))
    grip = *handle;
  else if (ms < hit.early())
    grip = Grip::Early;
  else if (ms > hit.late())
    grip = Grip::Late;

  drag = Drag{.grip = grip, .fromMs = ms, .start = hit};
  SetCapture(dialog);
  if (grip != Grip::Band)
    dragTo(dialog, x);
  return true;
}

/// Кончает перетаскивание: отпускает мышь и сохраняет окно попадания.
void endDrag(HWND dialog) {
  if (!drag)
    return;
  drag.reset();
  if (GetCapture() == dialog)
    ReleaseCapture();
  trainer().setSettings(trainer().settings());
  journalSettings("settings");
}

/// Ставит курсор «влево-вправо», если точка (`x`, `y`) окна над ползунком
/// шкалы.
///
/// @return правда, если курсор поставлен.
bool gaugeCursor(HWND dialog, int x, int y) {
  const Settings &settings = trainer().settings();
  const RECT box = gaugeRect(dialog);
  if (settings.panelCollapsed || x < box.left || x >= box.right || y < box.top ||
      y >= box.bottom)
    return false;
  if (!drag && !handleAt(gaugeLayout(box, settings.scaleMs), settings.window(), x))
    return false;
  if (drag && drag->grip == Grip::Band)
    return false;
  SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
  return true;
}

/// Показывает кнопку `control` в прямоугольнике `box` или прячет её; кнопку,
/// которая уже такая, не трогает — так она не мерцает на каждом тике.
void placeButton(HWND dialog, int control, bool shown, const RECT &box) {
  HWND button = GetDlgItem(dialog, control);
  if (!button)
    return;

  if (shown) {
    RECT now{};
    GetWindowRect(button, &now);
    POINT corner{.x = now.left, .y = now.top};
    ScreenToClient(dialog, &corner);
    if (corner.x != box.left || corner.y != box.top ||
        now.right - now.left != box.right - box.left ||
        now.bottom - now.top != box.bottom - box.top)
      SetWindowPos(button, nullptr, box.left, box.top, box.right - box.left,
                   box.bottom - box.top, SWP_NOZORDER | SWP_NOACTIVATE);
  }
  if ((IsWindowVisible(button) != 0) != shown)
    ShowWindow(button, shown ? SW_SHOW : SW_HIDE);
}

/// Меняет подпись кнопки `control`, только если она другая: так кнопка не
/// мерцает на каждом тике.
void setButtonText(HWND dialog, int control, const std::string &text) {
  std::array<char, 64> now{};
  GetDlgItemText(dialog, control, now.data(), static_cast<int>(now.size()));
  if (text != now.data())
    SetDlgItemText(dialog, control, text.c_str());
}

/// Показывает или прячет контрол `control`; контрол, который уже такой, не
/// трогает.
void showControl(HWND dialog, int control, bool shown) {
  HWND item = GetDlgItem(dialog, control);
  // Свой флаг контрола, а не IsWindowVisible: та смотрит и на окно, которое
  // при WM_INITDIALOG ещё не показано.
  if (item && ((GetWindowLong(item, GWL_STYLE) & WS_VISIBLE) != 0) != shown)
    ShowWindow(item, shown ? SW_SHOW : SW_HIDE);
}

/// Контролы панели настроек под верхней строкой, которые есть у любого
/// входа. Calibrate… сюда не входит: её прячет и показывает
/// `placeCalibrationButtons`.
constexpr std::array<int, 12> kPanelControls{
    IDC_MODE_LABEL,   IDC_MODE,       IDC_CHANNEL_LABEL, IDC_CHANNEL, IDC_INPUT_REFRESH,
    IDC_SHOW_NUMBERS, IDC_SHOW_STATS, IDC_OFFSET_LABEL,  IDC_OFFSET,  IDC_TOLERANCE_LABEL,
    IDC_TOLERANCE,    IDC_RANGE};

/// Контролы порога тишины: у входа MIDI их нет.
constexpr std::array<int, 2> kSilenceControls{IDC_SILENCE_LABEL, IDC_SILENCE};

/// Свёрнутость панели, с которой нарисована кнопка сворачивания; пусто —
/// кнопку ещё не рисовали.
std::optional<bool> drawnCollapsed;

/// Выбран ли вход MIDI, когда панель последний раз ставили; пусто — ещё не
/// ставили.
std::optional<bool> placedMidi;

/// Сворачивает или разворачивает панель настроек по настройкам, прячет
/// порог тишины у входа MIDI и перерисовывает кнопку сворачивания, если
/// стрелка на ней устарела. Когда меняется вид входа, пишет в журнал, откуда
/// начинаются строки тактов.
void placePanel(HWND dialog) {
  const Settings &settings = trainer().settings();
  const bool collapsed = settings.panelCollapsed;
  const bool midi = !settings.midiInput.empty();
  for (const int control : kPanelControls)
    showControl(dialog, control, !collapsed);
  for (const int control : kSilenceControls)
    showControl(dialog, control, !collapsed && !midi);
  if (drawnCollapsed != collapsed)
    InvalidateRect(GetDlgItem(dialog, IDC_SETTINGS_TOGGLE), nullptr, FALSE);
  if (placedMidi != midi) {
    placedMidi = midi;
    journal("panel for {} input: rows from {} px", midi ? "MIDI" : "audio",
            rowsArea(dialog).top);
  }
}

/// Рисует кнопку сворачивания `item`: рамку, стрелку — вниз у развёрнутой
/// панели, вправо у свёрнутой — и подпись шрифтом кнопки. Нажатая кнопка
/// утоплена: рамка наоборот, стрелка и подпись сдвинуты на пиксель.
void paintToggle(const DRAWITEMSTRUCT &item) {
  HDC context = item.hDC;
  const RECT &box = item.rcItem;
  const bool pressed = (item.itemState & ODS_SELECTED) != 0;
  const bool collapsed = trainer().settings().panelCollapsed;
  drawnCollapsed = collapsed;

  const double left = box.left;
  const double top = box.top;
  const double right = box.right;
  const double bottom = box.bottom;
  const int light = GetSysColor(pressed ? COLOR_3DSHADOW : COLOR_3DHILIGHT);
  const int dark = GetSysColor(pressed ? COLOR_3DHILIGHT : COLOR_3DSHADOW);
  fillBox(context, left, top, right, bottom, GetSysColor(COLOR_3DFACE));
  fillBox(context, left, top, right, top + 1.0, light);
  fillBox(context, left, top, left + 1.0, bottom, light);
  fillBox(context, left, bottom - 1.0, right, bottom, dark);
  fillBox(context, right - 1.0, top, right, bottom, dark);

  const double shift = pressed ? 1.0 : 0.0;
  const double height = bottom - top;
  const double size = std::max(5.0, height * 0.36);
  const double x = left + height * 0.35 + shift;
  const double y = (top + bottom) / 2.0 + shift;
  std::array<POINT, 3> arrow{};
  if (collapsed)
    arrow = {POINT{.x = pixel(x + size * 0.2), .y = pixel(y - size / 2.0)},
             POINT{.x = pixel(x + size * 0.2), .y = pixel(y + size / 2.0)},
             POINT{.x = pixel(x + size * 0.8), .y = pixel(y)}};
  else
    arrow = {POINT{.x = pixel(x), .y = pixel(y - size * 0.3)},
             POINT{.x = pixel(x + size), .y = pixel(y - size * 0.3)},
             POINT{.x = pixel(x + size / 2.0), .y = pixel(y + size * 0.3)}};
  const int text = GetSysColor(COLOR_BTNTEXT);
  HBRUSH brush = CreateSolidBrush(text);
  HPEN pen = CreatePen(PS_SOLID, 0, text);
  HGDIOBJ previousBrush = SelectObject(context, brush);
  HGDIOBJ previousPen = SelectObject(context, pen);
  Polygon(context, arrow.data(), static_cast<int>(arrow.size()));
  SelectObject(context, previousPen);
  SelectObject(context, previousBrush);
  DeleteObject(pen);
  DeleteObject(brush);

  HGDIOBJ previousFont = nullptr;
  // Win32 отдаёт шрифт контрола целым LRESULT: приведения к указателю не
  // избежать.
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  if (auto *font = reinterpret_cast<HFONT>(SendMessage(item.hwndItem, WM_GETFONT, 0, 0)))
    previousFont = SelectObject(context, font);
  SetBkMode(context, TRANSPARENT);
  SetTextColor(context, text);
  RECT label{.left = pixel(x + size + height * 0.25),
             .top = pixel(top + shift),
             .right = box.right,
             .bottom = pixel(bottom + shift)};
  DrawTextUTF8(context, "Settings", -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  if (previousFont)
    SelectObject(context, previousFont);
}

/// Ставит кнопки калибровки по состоянию тренажёра: Calibrate… доступна без
/// сессии при остановленном транспорте и с каналом, на итоге называется
/// Finish, а в свёрнутой панели настроек и у входа MIDI скрыта; Cancel — справа вверху
/// панели калибровки на шагах; Close, Calibrate again и Apply anyway — справа
/// внизу на итоге (design D7).
void placeCalibrationButtons(HWND dialog) {
  const auto &session = trainer().calibration();
  const bool finished = trainer().calibrationFinished();
  setButtonText(dialog, IDC_CALIBRATE, finished ? "Finish" : "Calibrate...");
  EnableWindow(GetDlgItem(dialog, IDC_CALIBRATE),
               finished || (!session && trainer().status().measuring == Measuring::Waiting));
  showControl(dialog, IDC_CALIBRATE,
              !trainer().settings().panelCollapsed && trainer().settings().midiInput.empty());

  const RECT area = rowsArea(dialog);
  const PanelLayout layout = panelLayout(area);
  const std::optional<Panel> panel = session ? std::optional(panelOf(*session)) : std::nullopt;
  const bool stepping = panel == Panel::Silence || panel == Panel::Notes;

  // Apply anyway — у итога с нотами, где порог не поставлен.
  std::optional<double> apply;
  if (session && session->calibration) {
    const std::optional<onset::CalibrationResult> &result = session->calibration->result();
    if (result && (result->verdict == onset::Verdict::NoiseTooClose ||
                   result->verdict == onset::Verdict::Clipping))
      apply = clamped(Settings{.silenceDb = result->thresholdDb}).silenceDb;
  }

  const auto box = [&](double right, int width, double top) {
    return RECT{.left = pixel(right) - width,
                .top = pixel(top),
                .right = pixel(right),
                .bottom = pixel(top) + kButtonHeight};
  };

  placeButton(dialog, IDC_CAL_CANCEL, stepping, box(layout.right, 80, layout.top - 2.0));

  double right = layout.right;
  placeButton(dialog, IDC_CAL_CLOSE, finished, box(right, 80, layout.buttonsTop));
  right -= 80 + kButtonGap;
  placeButton(dialog, IDC_CAL_AGAIN, finished, box(right, 130, layout.buttonsTop));
  right -= 130 + kButtonGap;
  if (apply)
    setButtonText(dialog, IDC_CAL_APPLY, fmt::format("Apply {} anyway", dbText(*apply)));
  placeButton(dialog, IDC_CAL_APPLY, apply.has_value(), box(right, 160, layout.buttonsTop));
}

/// Координата x точки мыши из `lParam` сообщения мыши.
int mouseX(LPARAM lParam) { return static_cast<short>(LOWORD(lParam)); }

/// Координата y точки мыши из `lParam` сообщения мыши.
int mouseY(LPARAM lParam) { return static_cast<short>(HIWORD(lParam)); }

INT_PTR CALLBACK proc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
  case WM_INITDIALOG:
    // Имена каналов REAPER отдаёт в UTF-8.
    WDL_UTF8_HookComboBox(GetDlgItem(dialog, IDC_CHANNEL));
    for (const char *name : kModeNames)
      SendDlgItemMessage(dialog, IDC_MODE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    fillControls(dialog);
    placeCalibrationButtons(dialog);
    return 1;

  case WM_COMMAND:
    onCommand(dialog, LOWORD(wParam), HIWORD(wParam));
    return 0;

  case WM_SIZE:
    placeCalibrationButtons(dialog);
    InvalidateRect(dialog, nullptr, FALSE);
    return 0;

  case WM_PAINT: {
    PAINTSTRUCT paint{};
    if (HDC context = BeginPaint(dialog, &paint)) {
      paintGaugeIn(dialog, context);
      paintRows(dialog, context);
      EndPaint(dialog, &paint);
    }
    return 1;
  }

  case WM_DRAWITEM: {
    // WM_DRAWITEM передаёт описание контрола указателем в целом LPARAM.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    const auto *item = reinterpret_cast<const DRAWITEMSTRUCT *>(lParam);
    if (!item || item->CtlID != IDC_SETTINGS_TOGGLE)
      return 0;
    paintToggle(*item);
    return 1;
  }

  case WM_LBUTTONDOWN:
    return startDrag(dialog, mouseX(lParam), mouseY(lParam)) ? 1 : 0;

  case WM_MOUSEMOVE:
    if (!drag)
      return 0;
    if (GetCapture() != dialog) {
      // Мышь отобрали, кнопку отпустили не над окном.
      endDrag(dialog);
      return 0;
    }
    gaugeCursor(dialog, mouseX(lParam), mouseY(lParam));
    dragTo(dialog, mouseX(lParam));
    return 1;

  case WM_LBUTTONUP:
    if (!drag)
      return 0;
    dragTo(dialog, mouseX(lParam));
    endDrag(dialog);
    return 1;

  case WM_CAPTURECHANGED:
    // Мышь отобрало другое окно: окно попадания, до которого дотянули,
    // сохраняется сразу, а не на следующем движении мыши над окном.
    endDrag(dialog);
    return 0;

  case WM_SETCURSOR: {
    POINT at{};
    GetCursorPos(&at);
    ScreenToClient(dialog, &at);
    if (!gaugeCursor(dialog, at.x, at.y))
      return 0;
#ifdef _WIN32
    // Иначе Windows вернёт курсор окна сразу после диалоговой процедуры.
    SetWindowLongPtr(dialog, DWLP_MSGRESULT, TRUE);
#endif
    return 1;
  }

  case WM_CLOSE:
    closeWindow();
    return 1;

  case WM_DESTROY:
    drag.reset();
    drawnCollapsed.reset();
    placedMidi.reset();
    if (window == dialog)
      window = nullptr;
    return 0;

  default:
    return 0;
  }
}

} // namespace

void setResourceModule(REAPER_PLUGIN_HINSTANCE module) { resourceModule = module; }

bool windowShown() { return window != nullptr; }

void toggleWindow() {
  if (window) {
    closeWindow();
    return;
  }

  window =
      CreateDialogParam(resourceModule, MAKEINTRESOURCE(IDD_TRAINER), GetMainHwnd(), proc, 0);
  if (!window) {
    journal("window: not created");
    return;
  }

  DockWindowAddEx(window, "Timing trainer", "reaper_training", true);
  DockWindowActivate(window);
  journal("window: shown");
}

void refreshWindow() {
  if (!window)
    return;

  showStatus(window);
  placePanel(window);
  placeCalibrationButtons(window);
  InvalidateRect(window, nullptr, FALSE);
}

void showSettings() {
  if (window)
    fillControls(window);
}

#ifdef TRAINING_DEBUG_BUILD
std::optional<bool> clickCheckbox(int control) {
  if (!window || !GetDlgItem(window, control))
    return std::nullopt;

  // Как щелчок мышью: флажок переключается, окно получает BN_CLICKED.
  const bool checked = IsDlgButtonChecked(window, control) != BST_CHECKED;
  CheckDlgButton(window, control, checked ? BST_CHECKED : BST_UNCHECKED);
  SendMessage(window, WM_COMMAND, MAKEWPARAM(control, BN_CLICKED),
              reinterpret_cast<LPARAM>(GetDlgItem(window, control)));
  return checked;
}

bool clickButton(int control) {
  HWND button = window ? GetDlgItem(window, control) : nullptr;
  if (!button || !IsWindowVisible(button) || !IsWindowEnabled(button))
    return false;

  SendMessage(window, WM_COMMAND, MAKEWPARAM(control, BN_CLICKED),
              reinterpret_cast<LPARAM>(button));
  return true;
}
#endif

#ifdef TRAINING_DEBUG_BUILD
bool dragGauge(double fromMs, double toMs) {
  if (!window || trainer().settings().panelCollapsed)
    return false;

  const GaugeLayout layout = gaugeLayout(gaugeRect(window), trainer().settings().scaleMs);
  const int y = pixel((layout.top + layout.bottom) / 2.0);
  const auto point = [&](double ms) {
    return static_cast<LPARAM>(MAKELONG(pixel(layout.x(ms)), y));
  };
  SendMessage(window, WM_LBUTTONDOWN, MK_LBUTTON, point(fromMs));
  SendMessage(window, WM_MOUSEMOVE, MK_LBUTTON, point(toMs));
  SendMessage(window, WM_LBUTTONUP, 0, point(toMs));
  return true;
}

std::optional<std::string> typeText(int control, const char *text) {
  HWND field = window ? GetDlgItem(window, control) : nullptr;
  if (!field || !IsWindowVisible(field) || !text || !*text)
    return std::nullopt;

  SetDlgItemText(window, control, text);
  SendMessage(window, WM_COMMAND, MAKEWPARAM(control, EN_CHANGE),
              reinterpret_cast<LPARAM>(field));
  SendMessage(window, WM_COMMAND, MAKEWPARAM(control, EN_KILLFOCUS),
              reinterpret_cast<LPARAM>(field));
  std::array<char, 64> left{};
  GetDlgItemText(window, control, left.data(), static_cast<int>(left.size()));
  return std::string(left.data());
}
#endif

#if defined(TRAINING_DEBUG_BUILD) && !defined(_WIN32)
namespace {

/// Рисует `paint` в память размером `width`×`height` точек и пишет картинку в
/// файл PPM `path`.
///
/// @return правда, если файл записан.
template <typename Paint>
bool snapshot(const char *path, int width, int height, const Paint &paint) {
  if (!path || width <= 0 || height <= 0)
    return false;

  HDC context = SWELL_CreateMemContext(nullptr, width, height);
  if (!context)
    return false;
  paint(context, RECT{.left = 0, .top = 0, .right = width, .bottom = height});

  // Кадр контекста — точки по строкам, 0xAARRGGBB; в PPM — R, G, B.
  const auto *pixels = static_cast<const unsigned int *>(SWELL_GetCtxFrameBuffer(context));
  std::FILE *file = pixels ? std::fopen(path, "wb") : nullptr;
  if (file) {
    std::fprintf(file, "P6\n%d %d\n255\n", width, height);
    std::vector<unsigned char> line(static_cast<std::size_t>(width) * 3);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const unsigned int pixel =
            pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                   static_cast<std::size_t>(x)];
        const auto at = static_cast<std::size_t>(x) * 3;
        line[at] = static_cast<unsigned char>(pixel >> 16U);
        line[at + 1] = static_cast<unsigned char>(pixel >> 8U);
        line[at + 2] = static_cast<unsigned char>(pixel);
      }
      std::fwrite(line.data(), 1, line.size(), file);
    }
    std::fclose(file);
  }
  SWELL_DeleteGfxContext(context);
  return file != nullptr;
}

} // namespace

bool snapshotGauge(const char *path, int width, int height) {
  return snapshot(path, width, height, [](HDC context, const RECT &area) {
    const Settings &settings = trainer().settings();
    paintGauge(context, area, settings.window(), settings.scaleMs);
  });
}

bool snapshotRows(const char *path, int width, int height) {
  return snapshot(path, width, height, paintArea);
}
#endif

void closeWindow() {
  if (!window)
    return;

  // Калибровка без окна не видна: закрытие окна её прерывает.
  trainer().endCalibration();

  HWND closing = window;
  window = nullptr;
  DockWindowRemove(closing);
  DestroyWindow(closing);
  journal("window: closed");
}

} // namespace training::reaper
