#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_GetMainHwnd
#define REAPERAPI_WANT_DockWindowAddEx
#define REAPERAPI_WANT_DockWindowRemove
#define REAPERAPI_WANT_DockWindowActivate
#define REAPERAPI_WANT_GetNumAudioInputs
#define REAPERAPI_WANT_GetInputChannelName
#define REAPERAPI_WANT_TrackCtl_SetToolTip

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

#include "training/core/clock.hpp"
#include "training/core/hit_window.hpp"
#include "training/core/settings.hpp"
#include "training/reaper/context_canvas.hpp"
#include "training/reaper/half_ms.hpp"
#include "training/reaper/mode.hpp"
#include "training/render/calibration/calibration_panel.hpp"
#include "training/render/calibration/db_text.hpp"
#include "training/render/common/fill.hpp"
#include "training/render/main/bar_rows.hpp"
#include "training/render/main/field_frame.hpp"
#include "training/render/main/field_texts.hpp"
#include "training/render/main/fit_text.hpp"
#include "training/render/main/gauge_layout.hpp"
#include "training/render/main/hit_window_gauge.hpp"
#include "training/render/main/ms_text.hpp"
#include "training/render/main/run_summary.hpp"
#include "training/render/main/settings_toggle.hpp"
#include "training/render/main/view_settings.hpp"
#include "training/render/palette.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
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
constexpr std::array<Mode, 5> kModes{Mode::Quarters, Mode::Eighths, Mode::EighthTriplets,
                                     Mode::Sixteenths, Mode::SixteenthTriplets};

constexpr std::array<const char *, 5> kModeNames{"Quarters", "Eighths", "Eighth triplets",
                                                 "Sixteenths", "Sixteenth triplets"};

/// Пока окно само пишет настройки в контролы, их уведомления не меняют
/// настройки обратно.
bool showingSettings = false;

/// Место режима `mode` в списке режимов окна, с 0; режима нет в списке — 0.
int modeIndex(Mode mode) {
  for (std::size_t i = 0; i < kModes.size(); ++i)
    if (kModes[i] == mode)
      return static_cast<int>(i);
  return 0;
}

/// Текст поля `control` без пробелов по краям.
std::string editText(HWND dialog, int control) {
  std::array<char, 32> text{};
  GetDlgItemText(dialog, control, text.data(), static_cast<int>(text.size()));
  std::string_view view(text.data());
  while (!view.empty() && view.front() == ' ')
    view.remove_prefix(1);
  while (!view.empty() && view.back() == ' ')
    view.remove_suffix(1);
  return std::string(view);
}

/// Поле окна `control` с числом настройки; пусто — `control` не Offset,
/// Tolerance, Silence или Hide bars.
std::optional<render::Field> fieldOf(int control) {
  switch (control) {
  case IDC_OFFSET:
    return render::Field::Offset;
  case IDC_TOLERANCE:
    return render::Field::Tolerance;
  case IDC_SILENCE:
    return render::Field::Silence;
  case IDC_HIDE_BARS:
    return render::Field::HideBars;
  default:
    return std::nullopt;
  }
}

/// Допустимые значения поля `field` при настройках ядра `settings` и шкале
/// настроек отрисовки `view`, в единицах поля: диапазон и то, от чего он
/// зависит.
render::FieldBounds boundsOf(render::Field field, const core::Settings &settings,
                             const render::ViewSettings &view) {
  const core::HitWindow &hit = settings.window();
  switch (field) {
  case render::Field::Offset: {
    const core::Range<core::Interval> range = view.offsetRange(hit);
    return {.min = range.min / core::Interval::kMillisecond,
            .max = range.max / core::Interval::kMillisecond,
            .condition = hit.tolerance() / core::Interval::kMillisecond};
  }
  case render::Field::Tolerance: {
    const core::Range<core::Interval> range = view.toleranceRange(hit);
    return {.min = range.min / core::Interval::kMillisecond,
            .max = range.max / core::Interval::kMillisecond,
            .condition = hit.offset() / core::Interval::kMillisecond};
  }
  case render::Field::HideBars:
    return {.min = render::ViewSettings::kHiddenBarsRange.min,
            .max = render::ViewSettings::kHiddenBarsRange.max,
            .condition = std::nullopt};
  case render::Field::Silence:
    break;
  }
  return {.min = kSilenceRange.min, .max = kSilenceRange.max, .condition = std::nullopt};
}

/// Действующее значение поля `field` в настройках ядра `settings` и
/// настройках отрисовки `view`, в единицах поля.
double currentOf(render::Field field, const core::Settings &settings,
                 const render::ViewSettings &view) {
  switch (field) {
  case render::Field::Offset:
    return settings.window().offset() / core::Interval::kMillisecond;
  case render::Field::Tolerance:
    return settings.window().tolerance() / core::Interval::kMillisecond;
  case render::Field::HideBars:
    return view.hiddenBars();
  case render::Field::Silence:
    break;
  }
  return settings.silenceDb();
}

/// Поле с числом, вписанное значение которого настройки не приняли.
struct Rejection {
  /// Поле: `IDC_OFFSET`, `IDC_TOLERANCE`, `IDC_SILENCE` или `IDC_HIDE_BARS`.
  int control = 0;

  /// Вписанный текст без пробелов по краям.
  std::string text;

  /// Правда, если у отклонений совпадают поле и текст.
  bool operator==(const Rejection &) const = default;
};

/// Отклонённое поле; пусто — вписанное в поля принято или не дописано.
std::optional<Rejection> rejection;

/// Окно попадания диапазоном для контролов окна: «+4…+20 ms», «-5…+20 ms».
std::string rangeText(const core::HitWindow &hit) {
  // Минус — дефис: контролы на Windows пишут растровым шрифтом диалога, в
  // котором есть «…», но нет «−» (U+2212).
  return fmt::format("{}…{} ms", render::halfMsText(hit.early(), render::MsStyle::Signed),
                     render::halfMsText(hit.late(), render::MsStyle::Signed));
}

/// Пишет настройки в журнал строкой «`what`: mode …»: по журналу проверки
/// видят их без снимка экрана.
void journalSettings(std::string_view what) {
  const core::Settings &settings = trainer().settings();
  const render::ViewSettings &view = trainer().view();
  journal("{}: mode {}, offset {} ms, tolerance {} ms, input {}, silence {} dBFS, bar "
          "numbers {}, median/iqr {}, hidden bars {}, collapsed {}",
          what, divisions(trainer().gridMode()),
          render::halfMsText(settings.window().offset(), render::MsStyle::Plain),
          render::halfMsText(settings.window().tolerance(), render::MsStyle::Plain),
          inputText(trainer().inputSettings()), settings.silenceDb(), view.showBarNumbers(),
          view.showBarStats(), view.hiddenBars(), view.panelCollapsed());
}

/// Правда, если строка состояния вместо обычного текста показывает
/// сообщение: об отклонённом поле или о не принятых строках
/// `reaper-extstate.ini`. Сообщение пишется красным.
bool statusAlert() { return rejection || !trainer().fileNotice().text().empty(); }

/// Строка состояния: сообщение об отклонённом поле (`render::rejectionText`),
/// иначе сообщение о не принятых строках `reaper-extstate.ini`
/// (`render::FileNotice`), иначе текст сессии калибровки, иначе обычный
/// текст сводки (`render::StatusLine::text`).
std::string statusText() {
  const Trainer &trainerRef = trainer();
  const render::StatusLine &status = trainerRef.status();
  const core::Settings &settings = trainerRef.settings();

  if (rejection)
    if (const std::optional<render::Field> field = fieldOf(rejection->control))
      return render::rejectionText(*field, rejection->text,
                                   boundsOf(*field, settings, trainerRef.view()),
                                   currentOf(*field, settings, trainerRef.view()));
  if (const std::string_view notice = trainerRef.fileNotice().text(); !notice.empty())
    return std::string(notice);

  if (trainerRef.calibrating()) {
    if (trainerRef.calibrationNoSound())
      return "Calibration finished";
    const auto state = trainerRef.calibrationState();
    switch (state ? state->step : core::calibration::Step::Silence) {
    case core::calibration::Step::Off:
    case core::calibration::Step::Silence:
      return fmt::format("Calibrating {}: step 1 of 2 — silence", trainerRef.channelName());
    case core::calibration::Step::Notes:
      return fmt::format("Calibrating {}: step 2 of 2 — notes", trainerRef.channelName());
    case core::calibration::Step::Done:
      break;
    }
    return "Calibration finished";
  }

  return status.text(kModeNames[static_cast<std::size_t>(modeIndex(trainerRef.gridMode()))],
                     rangeText(settings.window()), trainerRef.mode(),
                     trainerRef.calibrationCancelled());
}

/// Строка состояния, которая сейчас в окне, полностью — та, что в журнале.
std::string shownStatus;

/// Правда, если строка состояния в окне обрезана (`render::fitText`):
/// полный текст `shownStatus` — в подсказке у курсора.
bool statusCut = false;

/// Текст подписи строки состояния, видимая ширина, по которой его обрезали, и
/// полный текст: пока они те же, текст не меряется заново.
std::string fittedStatus;
int fittedWidth = -1;
std::string fittedFrom;

RECT controlRect(HWND dialog, int control);

/// Видимая часть строки состояния в окне `dialog`: подпись `IDC_STATUS`,
/// обрезанная правым краем окна, если окно в доке уже формы.
RECT statusRect(HWND dialog) {
  RECT box = controlRect(dialog, IDC_STATUS);
  RECT client{};
  GetClientRect(dialog, &client);
  box.right = std::max(box.left, std::min(box.right, client.right));
  return box;
}

/// Ширина строки `text` шрифтом подписи `label`, пикселей; 0 — контекст для
/// замера не получен.
double labelTextWidth(HWND label, std::string_view text) {
  HDC context = GetDC(label);
  if (!context)
    return 0.0;
  HGDIOBJ previous = nullptr;
  // Win32 отдаёт шрифт контрола целым LRESULT: приведения к указателю не
  // избежать.
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  if (auto *font = reinterpret_cast<HFONT>(SendMessage(label, WM_GETFONT, 0, 0)))
    previous = SelectObject(context, font);
  const std::string copy(text);
  RECT box{.left = 0, .top = 0, .right = 0, .bottom = 0};
  DrawTextUTF8(context, copy.c_str(), -1, &box,
               DT_LEFT | DT_TOP | DT_SINGLELINE | DT_CALCRECT);
  if (previous)
    SelectObject(context, previous);
  ReleaseDC(label, context);
  return static_cast<double>(box.right - box.left);
}

/// Пишет строку состояния в окно `dialog`, а когда она меняется, — в журнал:
/// по журналу проверки видят её без снимка экрана. Строку, которая не
/// помещается в видимую часть подписи (`statusRect`), обрезает с «...» в
/// конце (`render::fitText`). Цвет текста — по `statusAlert` в
/// `WM_CTLCOLORSTATIC`.
void showStatus(HWND dialog) {
  const std::string text = statusText();
  if (text != shownStatus) {
    journal("window status: {}", text);
    shownStatus = text;
  }

  const RECT box = statusRect(dialog);
  const int width = static_cast<int>(box.right - box.left);
  if (text != fittedFrom || width != fittedWidth) {
    HWND label = GetDlgItem(dialog, IDC_STATUS);
    fittedStatus = render::fitText(
        text, width, [label](std::string_view part) { return labelTextWidth(label, part); });
    fittedFrom = text;
    fittedWidth = width;
  }
  statusCut = fittedStatus != text;
  SetDlgItemText(dialog, IDC_STATUS, fittedStatus.c_str());
}

/// Пишет окно попадания из настроек в поля Offset и Tolerance, кроме поля
/// `editing`, в котором сейчас пишут (0 — в оба), и диапазоном рядом со
/// шкалой.
void showHitWindow(HWND dialog, int editing = 0) {
  const bool showing = showingSettings;
  showingSettings = true;
  const core::HitWindow hit = trainer().settings().window();
  if (editing != IDC_OFFSET)
    SetDlgItemText(dialog, IDC_OFFSET,
                   render::halfMsText(hit.offset(), render::MsStyle::Signed).c_str());
  if (editing != IDC_TOLERANCE)
    SetDlgItemText(dialog, IDC_TOLERANCE,
                   render::halfMsText(hit.tolerance(), render::MsStyle::Plain).c_str());
  SetDlgItemText(dialog, IDC_RANGE, rangeText(hit).c_str());
  showingSettings = showing;
}

/// Пишет порог тишины из настроек в поле Silence.
void showSilence(HWND dialog) {
  const bool showing = showingSettings;
  showingSettings = true;
  SetDlgItemText(dialog, IDC_SILENCE,
                 fmt::format("{}", std::lround(trainer().settings().silenceDb())).c_str());
  showingSettings = showing;
}

/// Пишет число скрытых тактов из настроек отрисовки в поле Hide bars.
void showHiddenBars(HWND dialog) {
  const bool showing = showingSettings;
  showingSettings = true;
  SetDlgItemText(dialog, IDC_HIDE_BARS,
                 fmt::format("{}", trainer().view().hiddenBars()).c_str());
  showingSettings = showing;
}

void placePanel(HWND dialog);
RECT rowsArea(HWND dialog);
std::optional<RECT> indicatorsBeside(HWND dialog);

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
/// пропавший канал. Перед этим тренажёр перечитывает сведения о выбранном
/// входе для строки состояния (`Trainer::refreshInputs`).
void fillInputs(HWND dialog) {
  const bool showing = showingSettings;
  showingSettings = true;
  // Звуковую карту могли сменить, а устройство MIDI — подключить, отключить
  // или включить в настройках REAPER.
  trainer().refreshInputs();
  const bool midi = trainer().inputSettings().input() == Input::Midi;
  const int channel = trainer().inputSettings().channel();
  const std::optional<MidiInput> &chosen = trainer().midiInput();

  inputChoices.clear();
  std::vector<std::string> labels;
  int selected = -1;

  const int channels = GetNumAudioInputs();
  for (int i = 0; i < channels; ++i) {
    const char *name = GetInputChannelName(i);
    if (!midi && channel == i)
      selected = static_cast<int>(labels.size());
    labels.push_back(fmt::format("{}: {}", i + 1, name ? name : ""));
    inputChoices.push_back({.channel = i, .midi = {}});
  }
  for (MidiInput &input : midiInputs()) {
    if (midi && chosen && chosen->device == input.device)
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

void setRejection(HWND dialog, std::optional<Rejection> next);

/// Пишет настройки в контролы окна; вписанное в поля, которое настройки не
/// приняли, заменяется действующими значениями.
void fillControls(HWND dialog) {
  showingSettings = true;
  const render::ViewSettings &view = trainer().view();

  SendDlgItemMessage(dialog, IDC_MODE, CB_SETCURSEL,
                     static_cast<WPARAM>(modeIndex(trainer().gridMode())), 0);
  showHitWindow(dialog);
  showSilence(dialog);
  showHiddenBars(dialog);
  setRejection(dialog, std::nullopt);
  CheckDlgButton(dialog, IDC_SHOW_NUMBERS,
                 view.showBarNumbers() ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(dialog, IDC_SHOW_STATS, view.showBarStats() ? BST_CHECKED : BST_UNCHECKED);

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

/// Ставит в выбранный вход `input` вход, выбранный в списке Input окна
/// `dialog`.
///
/// @return ложь, если в списке ничего не выбрано: настройки не меняются.
bool chooseInput(HWND dialog, InputSettings &input) {
  const auto index = SendDlgItemMessage(dialog, IDC_CHANNEL, CB_GETCURSEL, 0, 0);
  if (index < 0 || index >= static_cast<LRESULT>(inputChoices.size()))
    return false;

  const InputChoice &choice = inputChoices[static_cast<std::size_t>(index)];
  if (choice.channel >= 0) {
    // Каналы списка — каналы звуковой карты с 0: сеттер их принимает.
    input.setChannel(choice.channel);
    input.setMidiInput({});
    input.setMidiIndex(-1);
    input.setInput(Input::Audio);
  } else {
    input.setMidiInput(choice.midi.key);
    input.setMidiIndex(choice.midi.device);
    input.setInput(Input::Midi);
  }
  return true;
}

/// Ставит и сохраняет настройки, правленные контролом `control` с
/// уведомлением `notification`: режим сетки — в тренажёр, вход — в выбранный
/// вход, колонки строк и свёрнутость панели — в настройки отрисовки. Правка
/// полей с числом — `editField`.
///
/// @return ложь, если уведомление настроек не меняет: контрол не настройка
///   или в списке ничего не выбрано.
bool editSettings(HWND dialog, int control, int notification) {
  const core::Settings settings = trainer().settings();
  InputSettings input = trainer().inputSettings();
  render::ViewSettings view = trainer().view();
  if (notification == CBN_SELCHANGE && control == IDC_MODE) {
    const auto index = SendDlgItemMessage(dialog, IDC_MODE, CB_GETCURSEL, 0, 0);
    if (index < 0 || index >= static_cast<LRESULT>(kModes.size()))
      return false;
    trainer().setGridMode(kModes[static_cast<std::size_t>(index)]);
  } else if (notification == CBN_SELCHANGE && control == IDC_CHANNEL) {
    if (!chooseInput(dialog, input))
      return false;
    trainer().setSettings(settings, input);
  } else if (notification == BN_CLICKED && control == IDC_SETTINGS_TOGGLE) {
    view.setPanelCollapsed(!view.panelCollapsed());
    trainer().setView(view);
  } else if (notification == BN_CLICKED && control == IDC_SHOW_NUMBERS) {
    view.setShowBarNumbers(IsDlgButtonChecked(dialog, IDC_SHOW_NUMBERS) == BST_CHECKED);
    trainer().setView(view);
  } else if (notification == BN_CLICKED && control == IDC_SHOW_STATS) {
    view.setShowBarStats(IsDlgButtonChecked(dialog, IDC_SHOW_STATS) == BST_CHECKED);
    trainer().setView(view);
  } else {
    return false;
  }
  return true;
}

/// Показывает правку настроек контролом `control` в окне: правка снимает
/// сообщение о не принятых строках `reaper-extstate.ini`
/// (`render::FileNotice::onSettingsEdit`).
void showEdit(HWND dialog, int control) {
  trainer().fileNotice().onSettingsEdit();
  journalSettings("settings");
  if (control == IDC_OFFSET || control == IDC_TOLERANCE)
    showHitWindow(dialog, control);
  const bool collapsed = trainer().view().panelCollapsed();
  // Развёрнутые настройки — показ настроек: устройство MIDI могли подключить,
  // отключить или включить в настройках REAPER, пока они были свёрнуты.
  if (control == IDC_SETTINGS_TOGGLE && !collapsed)
    fillInputs(dialog);
  refreshWindow();
  if (control == IDC_SETTINGS_TOGGLE)
    journal("panel {}: rows from {} px", collapsed ? "collapsed" : "expanded",
            rowsArea(dialog).top);
}

/// Ставит в настройки ядра `settings` число `value` поля `control` — Offset
/// или Tolerance, мс, Silence, dBFS. Окно попадания должно лежать в шкале
/// настроек отрисовки (`render::ViewSettings::fits`).
///
/// @return ложь, если число отклонено: настройки прежние.
bool setField(core::Settings &settings, int control, double value) {
  const core::HitWindow &hit = settings.window();
  std::optional<core::HitWindow> edited;
  switch (control) {
  case IDC_OFFSET:
    edited = core::HitWindow::create(value * core::Interval::kMillisecond, hit.tolerance());
    break;
  case IDC_TOLERANCE:
    edited = core::HitWindow::create(hit.offset(), value * core::Interval::kMillisecond);
    break;
  default:
    if (!kSilenceRange.contains(value))
      return false;
    settings.setSilenceDb(value);
    return true;
  }
  if (!edited || !trainer().view().fits(*edited))
    return false;
  settings.setWindow(*edited);
  return true;
}

/// Ставит в настройки отрисовки число скрытых тактов, вписанное в поле Hide
/// bars, как `editField`.
void editHiddenBars(HWND dialog) {
  const std::string text = editText(dialog, IDC_HIDE_BARS);
  const std::optional<int> bars = integerOf(text);
  if (!bars) {
    if (rejection && rejection->control == IDC_HIDE_BARS)
      setRejection(dialog, std::nullopt);
    return;
  }
  render::ViewSettings view = trainer().view();
  if (!view.setHiddenBars(*bars)) {
    setRejection(dialog, Rejection{.control = IDC_HIDE_BARS, .text = text});
    return;
  }
  setRejection(dialog, std::nullopt);
  trainer().setView(view);
  showEdit(dialog, IDC_HIDE_BARS);
}

/// Ставит в настройки число, вписанное в поле `control` — Offset, Tolerance,
/// Silence или Hide bars. Недописанное число настроек не меняет. Допустимое —
/// сразу действует и сохраняется (`Trainer::setSettings`, `showEdit`).
/// Недопустимое настройки не меняет, а поле с вписанным становится
/// отклонённым (`setRejection`).
void editField(HWND dialog, int control) {
  if (control == IDC_HIDE_BARS) {
    editHiddenBars(dialog);
    return;
  }
  const std::string text = editText(dialog, control);
  // Порог тишины — целое; смещение и допуск округляются до 0.5 мс.
  std::optional<double> value;
  if (control == IDC_SILENCE) {
    if (const std::optional<int> silence = integerOf(text))
      value = *silence;
  } else if (const std::optional<core::Interval> ms = halfMsOf(text)) {
    value = *ms / core::Interval::kMillisecond;
  }

  if (!value) {
    // Недописанное число — не отклонённое значение: рамки и сообщения нет.
    if (rejection && rejection->control == control)
      setRejection(dialog, std::nullopt);
    return;
  }

  core::Settings settings = trainer().settings();
  if (!setField(settings, control, *value)) {
    setRejection(dialog, Rejection{.control = control, .text = text});
    return;
  }
  setRejection(dialog, std::nullopt);
  trainer().setSettings(settings);
  showEdit(dialog, control);
}

/// Обработчик команды контрола `control` окна `dialog` с уведомлением
/// `notification`: кнопки калибровки и обновления входов, ввод в поле с числом
/// и уход из него, правка остальных контролов настроек. Пока окно само пишет
/// настройки в контролы, ничего не делает.
void onCommand(HWND dialog, int control, int notification) {
  if (showingSettings)
    return;
  if (notification == BN_CLICKED && onCalibrationButton(control))
    return;
  if (notification == BN_CLICKED && control == IDC_INPUT_REFRESH) {
    // Устройство MIDI могли подключить или включить в настройках REAPER:
    // fillInputs перечитывает и вход, и сведения строки состояния.
    fillInputs(dialog);
    return;
  }
  if (fieldOf(control)) {
    if (notification == EN_KILLFOCUS) {
      // Ушли из поля — в нём действующее значение, вписанное не принятым не
      // считается.
      setRejection(dialog, std::nullopt);
      if (control == IDC_SILENCE)
        showSilence(dialog);
      else if (control == IDC_HIDE_BARS)
        showHiddenBars(dialog);
      else
        showHitWindow(dialog);
    } else if (notification == EN_CHANGE) {
      editField(dialog, control);
    }
    return;
  }

  if (editSettings(dialog, control, notification))
    showEdit(dialog, control);
}

using render::dbText;

/// Округляет координату до пикселя.
int pixel(double value) { return static_cast<int>(std::lround(value)); }

/// Компоненты окна (`render::Element`): перед рисованием окно ставит им
/// состояние из тренажёра и настроек. Шкала окна попадания и панель
/// калибровки берут настройки ядра тренажёра сами.
struct Components {
  /// Компоненты окна; шкала и панель калибровки — с настройками ядра
  /// тренажёра `settings`.
  explicit Components(const core::Settings &settings)
      : gauge(settings), calibrationPanel("REAPER", settings) {}

  render::HitWindowGauge gauge;
  render::FieldFrame fieldFrame{render::Color{}};
  render::RunSummary runSummary;
  render::BarRows barRows;
  render::CalibrationPanel calibrationPanel;
  render::SettingsToggle settingsToggle;
};

/// Компоненты окна: есть, пока есть окно.
std::optional<Components> components;

/// Ставит строкам тактов `barRows` такты запуска тренажёра и настройки
/// отрисовки: колонки и скрытые такты.
void setRows(render::BarRows &barRows) {
  const render::ViewSettings &view = trainer().view();
  barRows.setColumns(view.showBarNumbers(), view.showBarStats());
  barRows.setBars(trainer().timeline(), trainer().heard(),
                  trainer().mode() == core::state::WorkMode::Run,
                  trainer().settings().window(), view.hiddenBars());
}

/// Ставит индикаторам запуска сводку тактов тренажёра, показанных строками
/// тактов `barRows`: в запуске — по последним нотам, после остановки — по
/// всему запуску.
void setRunSummary(render::RunSummary &runSummary, const render::BarRows &barRows) {
  runSummary.setRun(trainer().timeline(), trainer().settings().window(),
                    trainer().mode() == core::state::WorkMode::Run, barRows.shownEnd());
}

/// Рисует на холсте `canvas` в области `area` строки тактов, а над ними
/// полосой высотой в строку такта — индикаторы запуска, если `strip`; пока
/// идёт калибровка — её панель.
void paintArea(render::Canvas &canvas, const RECT &area, bool strip) {
  if (!components)
    return;
  render::CalibrationPanel &calibrationPanel = components->calibrationPanel;
  render::BarRows &barRows = components->barRows;
  render::RunSummary &runSummary = components->runSummary;
  render::Frame frame{.canvas = canvas, .area = boxOf(area)};
  if (trainer().calibrating()) {
    const auto state = trainer().calibrationState();
    calibrationPanel.setNoSound(trainer().calibrationNoSound());
    calibrationPanel.setSession(state ? &*state : nullptr, trainer().channelName(),
                                trainer().calibrationFromDb(),
                                trainer().calibrationHeardSound());
    calibrationPanel.draw(frame);
    return;
  }

  setRows(barRows);
  if (strip) {
    const double height = (frame.area.bottom - frame.area.top) / (render::kRows + 1);
    setRunSummary(runSummary, barRows);
    runSummary.draw(frame.within(render::Box{.left = frame.area.left,
                                             .top = frame.area.top,
                                             .right = frame.area.right,
                                             .bottom = frame.area.top + height}));
    frame.area.top += height;
  }

  barRows.draw(frame);
}

/// Область строк тактов и панели калибровки: всё под контролами окна — под
/// третьей строкой настроек, где поле Hide bars есть у любого входа; у
/// свёрнутой панели настроек — под кнопкой сворачивания.
RECT rowsArea(HWND dialog) {
  RECT client{};
  GetClientRect(dialog, &client);

  const int lowest = trainer().view().panelCollapsed() ? IDC_SETTINGS_TOGGLE : IDC_HIDE_BARS;

  RECT controls{};
  GetWindowRect(GetDlgItem(dialog, lowest), &controls);
  POINT bottom{.x = controls.left, .y = controls.bottom};
  ScreenToClient(dialog, &bottom);

  RECT area = client;
  area.top = bottom.y + 6;
  return area;
}

/// Рисует на холсте `canvas` строки тактов или панель калибровки под
/// контролами окна; индикаторы запуска, которым нет места справа от панели
/// настроек (`indicatorsBeside`), — полосой над строками тактов.
void paintRowsIn(HWND dialog, render::Canvas &canvas) {
  const RECT area = rowsArea(dialog);
  if (area.bottom > area.top)
    paintArea(canvas, area, trainer().view().showBarStats() && !indicatorsBeside(dialog));
}

/// Прямоугольник контрола `control` в окне `dialog`, в координатах окна.
RECT controlRect(HWND dialog, int control) {
  RECT box{};
  GetWindowRect(GetDlgItem(dialog, control), &box);
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

/// Прямоугольник шкалы окна попадания в окне — место невидимого контрола
/// `IDC_GAUGE`.
RECT gaugeRect(HWND dialog) { return controlRect(dialog, IDC_GAUGE); }

/// Рисует на холсте `canvas` шкалу окна попадания, если панель настроек
/// развёрнута.
void paintGaugeIn(HWND dialog, render::Canvas &canvas) {
  const render::ViewSettings &view = trainer().view();
  if (view.panelCollapsed() || !components)
    return;
  render::HitWindowGauge &gauge = components->gauge;
  gauge.setColors(dialogColors());
  gauge.setScale(view.scale());
  gauge.draw(render::Frame{.canvas = canvas, .area = boxOf(gaugeRect(dialog))});
}

/// Поля с числом настройки.
constexpr std::array<int, 4> kNumberFields{IDC_OFFSET, IDC_TOLERANCE, IDC_SILENCE,
                                           IDC_HIDE_BARS};

/// Полоса вокруг поля `control` шириной `render::kRejectionFrame`:
/// прямоугольник поля, расширенный на неё.
RECT frameBand(HWND dialog, int control) {
  RECT band = controlRect(dialog, control);
  band.left -= render::kRejectionFrame;
  band.top -= render::kRejectionFrame;
  band.right += render::kRejectionFrame;
  band.bottom += render::kRejectionFrame;
  return band;
}

/// Перерисовывает полосу вокруг поля `control` (`frameBand`).
void invalidateBand(HWND dialog, int control) {
  const RECT band = frameBand(dialog, control);
  InvalidateRect(dialog, &band, FALSE);
}

/// Рисует на холсте `canvas` вокруг видимых полей с числом рамку в полосе
/// `frameBand` (`render::FieldFrame`): у отклонённого поля — красную, у
/// остальных — цветом фона диалога, стирая прежнюю.
void paintFieldFrames(HWND dialog, render::Canvas &canvas) {
  if (!components)
    return;
  render::FieldFrame &fieldFrame = components->fieldFrame;
  const render::DialogColors colors = dialogColors();
  for (const int control : kNumberFields) {
    HWND field = GetDlgItem(dialog, control);
    if (!field || (GetWindowLong(field, GWL_STYLE) & WS_VISIBLE) == 0)
      continue;
    fieldFrame.setColor(rejection && rejection->control == control ? render::kBad
                                                                   : colors.face);
    fieldFrame.draw(
        render::Frame{.canvas = canvas, .area = boxOf(frameBand(dialog, control))});
  }
}

/// Ставит отклонённое поле `next`; пусто — отклонённого поля нет. Если
/// отклонённое поле меняется, перерисовывает полосы вокруг прежнего и нового
/// поля и строку состояния.
void setRejection(HWND dialog, std::optional<Rejection> next) {
  if (rejection == next)
    return;
  if (rejection)
    invalidateBand(dialog, rejection->control);
  rejection = std::move(next);
  if (rejection)
    invalidateBand(dialog, rejection->control);
  showStatus(dialog);
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
  core::Interval from;
  core::HitWindow start;
};

/// Идущее перетаскивание; пусто — мышь на шкале не нажата.
std::optional<Drag> drag;

/// Ползунок под точкой `x` окна, если он ближе `render::kHandleReach`
/// пикселей; пусто — ни одного.
std::optional<Grip> handleAt(const render::GaugeLayout &layout, const core::HitWindow &hit,
                             double x) {
  const double early = std::abs(x - layout.x(hit.early()));
  const double late = std::abs(x - layout.x(hit.late()));
  if (early <= render::kHandleReach && early <= late)
    return Grip::Early;
  if (late <= render::kHandleReach)
    return Grip::Late;
  return std::nullopt;
}

/// Ставит окно попадания `hit` в настройки без сохранения и показывает его
/// в полях, на шкале и в строке состояния.
void previewWindow(HWND dialog, const core::HitWindow &hit) {
  // Ползунки дают только окно в шкале.
  core::Settings settings = trainer().settings();
  if (settings.window() == hit)
    return;
  settings.setWindow(hit);
  trainer().previewSettings(settings);
  showHitWindow(dialog);
  refreshWindow();
}

/// Двигает то, что тянут, к точке `x` окна.
void dragTo(HWND dialog, double x) {
  if (!drag)
    return;
  const render::ViewSettings &view = trainer().view();
  const render::GaugeLayout layout =
      render::gaugeLayout(boxOf(gaugeRect(dialog)), view.scale());
  const core::Interval value = layout.valueAt(x);
  const core::HitWindow &hit = trainer().settings().window();
  switch (drag->grip) {
  case Grip::Early:
    previewWindow(dialog, view.withEarly(hit, value));
    return;
  case Grip::Late:
    previewWindow(dialog, view.withLate(hit, value));
    return;
  case Grip::Band:
    break;
  }
  previewWindow(dialog, view.shifted(drag->start, value - drag->from));
}

/// Начинает перетаскивание, если точка (`x`, `y`) окна на шкале: ползунок
/// рядом — его край, щелчок в окне попадания — полосу, иначе ближайший
/// ползунок переносится к щелчку. Берёт мышь окну.
///
/// @return ложь, если точка не на шкале или панель свёрнута.
bool startDrag(HWND dialog, int x, int y) {
  const render::ViewSettings &view = trainer().view();
  const RECT box = gaugeRect(dialog);
  if (view.panelCollapsed() || x < box.left || x >= box.right || y < box.top ||
      y >= box.bottom)
    return false;

  const core::HitWindow hit = trainer().settings().window();
  const render::GaugeLayout layout = render::gaugeLayout(boxOf(box), view.scale());
  const core::Interval value = layout.valueAt(x);
  Grip grip = Grip::Band;
  if (const auto handle = handleAt(layout, hit, x))
    grip = *handle;
  else if (value < hit.early())
    grip = Grip::Early;
  else if (value > hit.late())
    grip = Grip::Late;

  drag = Drag{.grip = grip, .from = value, .start = hit};
  SetCapture(dialog);
  if (grip != Grip::Band)
    dragTo(dialog, x);
  return true;
}

/// Кончает перетаскивание: отпускает мышь и сохраняет окно попадания —
/// правку настроек в окне (`render::FileNotice::onSettingsEdit`).
void endDrag(HWND dialog) {
  if (!drag)
    return;
  drag.reset();
  if (GetCapture() == dialog)
    ReleaseCapture();
  trainer().setSettings(trainer().settings());
  trainer().fileNotice().onSettingsEdit();
  journalSettings("settings");
  refreshWindow();
}

/// Ставит курсор «влево-вправо», если точка (`x`, `y`) окна над ползунком
/// шкалы.
///
/// @return правда, если курсор поставлен.
bool gaugeCursor(HWND dialog, int x, int y) {
  const render::ViewSettings &view = trainer().view();
  const RECT box = gaugeRect(dialog);
  if (view.panelCollapsed() || x < box.left || x >= box.right || y < box.top ||
      y >= box.bottom)
    return false;
  if (!drag && !handleAt(render::gaugeLayout(boxOf(box), view.scale()),
                         trainer().settings().window(), x))
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
  // Свой флаг кнопки, а не IsWindowVisible: та смотрит и на окно, которое при
  // WM_INITDIALOG ещё не показано, и кнопка осталась бы видна.
  if (((GetWindowLong(button, GWL_STYLE) & WS_VISIBLE) != 0) != shown)
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
constexpr std::array<int, 14> kPanelControls{
    IDC_MODE_LABEL,   IDC_MODE,       IDC_CHANNEL_LABEL,   IDC_CHANNEL,  IDC_INPUT_REFRESH,
    IDC_SHOW_NUMBERS, IDC_SHOW_STATS, IDC_OFFSET_LABEL,    IDC_OFFSET,   IDC_TOLERANCE_LABEL,
    IDC_TOLERANCE,    IDC_RANGE,      IDC_HIDE_BARS_LABEL, IDC_HIDE_BARS};

/// Контролы порога тишины: у входа MIDI их нет.
constexpr std::array<int, 2> kSilenceControls{IDC_SILENCE_LABEL, IDC_SILENCE};

/// Место справа от панели настроек до правого края окна, по высоте — до
/// строк тактов. Панель кончается на отступе правее самого правого видимого
/// контрола; отступ — тот же, что у контролов от левого края окна.
RECT besidePanel(HWND dialog) {
  int right = 0;
  const auto widen = [&](int control) {
    HWND item = GetDlgItem(dialog, control);
    if (item && (GetWindowLong(item, GWL_STYLE) & WS_VISIBLE) != 0)
      right = std::max(right, static_cast<int>(controlRect(dialog, control).right));
  };
  widen(IDC_SETTINGS_TOGGLE);
  widen(IDC_STATUS);
  widen(IDC_CALIBRATE);
  for (const int control : kPanelControls)
    widen(control);
  for (const int control : kSilenceControls)
    widen(control);

  RECT client{};
  GetClientRect(dialog, &client);
  const int pad = static_cast<int>(controlRect(dialog, IDC_SETTINGS_TOGGLE).left);
  return RECT{.left = std::min(right + pad, static_cast<int>(client.right)),
              .top = client.top,
              .right = client.right,
              .bottom = rowsArea(dialog).top};
}

/// Место индикаторов запуска справа от панели настроек (`besidePanel`);
/// пусто — индикаторы встают полосой над строками тактов: настройки свёрнуты
/// или справа уже `render::RunSummary::minWidth`.
std::optional<RECT> indicatorsBeside(HWND dialog) {
  if (trainer().view().panelCollapsed())
    return std::nullopt;
  const RECT beside = besidePanel(dialog);
  if (beside.right - beside.left < render::RunSummary::minWidth())
    return std::nullopt;
  return beside;
}

/// Рисует на холсте `canvas` справа от панели настроек индикаторы запуска,
/// если им там есть место и видна колонка медианы и IQR; иначе закрашивает
/// это место цветом фона диалога, стирая прежние индикаторы.
void paintIndicatorsIn(HWND dialog, render::Canvas &canvas) {
  if (!components)
    return;
  const RECT beside = besidePanel(dialog);
  if (beside.right <= beside.left || beside.bottom <= beside.top)
    return;
  const render::Frame frame{.canvas = canvas, .area = boxOf(beside)};
  if (trainer().view().showBarStats() && indicatorsBeside(dialog)) {
    setRows(components->barRows);
    setRunSummary(components->runSummary, components->barRows);
    components->runSummary.draw(frame);
    return;
  }
  render::Fill(dialogColors().face).draw(frame);
}

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
  const bool collapsed = trainer().view().panelCollapsed();
  const bool midi = trainer().inputSettings().input() == Input::Midi;
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

/// Рисует кнопку сворачивания `item` (`render::SettingsToggle`) шрифтом кнопки
/// и запоминает, с какой свёрнутостью панели она нарисована.
void paintToggle(const DRAWITEMSTRUCT &item) {
  if (!components)
    return;
  render::SettingsToggle &settingsToggle = components->settingsToggle;
  const bool collapsed = trainer().view().panelCollapsed();
  drawnCollapsed = collapsed;
  // Win32 отдаёт шрифт контрола целым LRESULT: приведения к указателю не
  // избежать.
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  auto *font = reinterpret_cast<HFONT>(SendMessage(item.hwndItem, WM_GETFONT, 0, 0));
  ContextCanvas canvas(item.hDC, font);
  settingsToggle.setColors(dialogColors());
  settingsToggle.setPressed((item.itemState & ODS_SELECTED) != 0);
  settingsToggle.setCollapsed(collapsed);
  settingsToggle.draw(render::Frame{.canvas = canvas, .area = boxOf(item.rcItem)});
}

/// Ставит кнопки калибровки по состоянию тренажёра: Calibrate… доступна без
/// сессии в режиме работы «ожидание» и с каналом, на итоге называется
/// Finish, а в свёрнутой панели настроек и у входа MIDI скрыта; Cancel —
/// справа вверху панели калибровки на шагах; Close, Calibrate again и Apply
/// anyway — справа внизу на итоге.
void placeCalibrationButtons(HWND dialog) {
  const bool session = trainer().calibrating();
  const bool finished = trainer().calibrationFinished();
  setButtonText(dialog, IDC_CALIBRATE, finished ? "Finish" : "Calibrate...");
  EnableWindow(GetDlgItem(dialog, IDC_CALIBRATE),
               finished || (!session && trainer().mode() == core::state::WorkMode::Idle &&
                            trainer().status().input == render::InputStatus::Present));
  showControl(dialog, IDC_CALIBRATE,
              !trainer().view().panelCollapsed() &&
                  trainer().inputSettings().input() == Input::Audio);

  const render::PanelLayout layout =
      render::CalibrationPanel::layoutOf(boxOf(rowsArea(dialog)));
  const auto state = trainer().calibrationState();
  const bool stepping = session && !trainer().calibrationNoSound() &&
                        (!state || state->step == core::calibration::Step::Silence ||
                         state->step == core::calibration::Step::Notes);
  const std::optional<double> apply = trainer().anywaySilenceDb();

  const auto box = [&](double right, int width, double top) {
    return RECT{.left = pixel(right) - width,
                .top = pixel(top),
                .right = pixel(right),
                .bottom = pixel(top) + render::kButtonHeight};
  };

  placeButton(dialog, IDC_CAL_CANCEL, stepping, box(layout.right, 80, layout.top - 2.0));

  double right = layout.right;
  placeButton(dialog, IDC_CAL_CLOSE, finished, box(right, 80, layout.buttonsTop));
  right -= 80 + render::kButtonGap;
  placeButton(dialog, IDC_CAL_AGAIN, finished, box(right, 130, layout.buttonsTop));
  right -= 130 + render::kButtonGap;
  if (apply)
    setButtonText(dialog, IDC_CAL_APPLY, fmt::format("Apply {} anyway", dbText(*apply)));
  placeButton(dialog, IDC_CAL_APPLY, apply.has_value(), box(right, 160, layout.buttonsTop));
}

/// Правда, пока у курсора стоит подсказка, которую поставило окно.
bool tipShown = false;

/// Подсказка у курсора над контролом `over` в точке `at` окна `dialog`:
/// допустимые значения поля с числом (`render::rangeHint`) или полный текст
/// обрезанной строки состояния. Строка состояния ищется по месту курсора: на
/// Windows подпись для мыши прозрачна, и `over` — само окно.
///
/// @return пусто — над этим местом подсказки нет.
std::optional<std::string> tipText(HWND dialog, HWND over, POINT at) {
  for (const int control : kNumberFields)
    if (over && over == GetDlgItem(dialog, control))
      if (const std::optional<render::Field> field = fieldOf(control))
        return render::rangeHint(*field,
                                 boundsOf(*field, trainer().settings(), trainer().view()));

  const RECT status = statusRect(dialog);
  if (statusCut && at.x >= status.left && at.x < status.right && at.y >= status.top &&
      at.y < status.bottom)
    return shownStatus;
  return std::nullopt;
}

/// Ставит у курсора подсказку `tipText` на 20 пикселей ниже курсора, чтобы
/// она не закрывала то, над чем курсор. Где подсказки нет, убирает
/// подсказку, которую ставило окно. REAPER убирает подсказку сам, когда мышь
/// отходит от точки показа, поэтому подсказка ставится заново при каждом
/// вызове.
///
/// @return правда, если подсказка поставлена.
bool showTip(HWND dialog, HWND over) {
  POINT screen{};
  GetCursorPos(&screen);
  POINT at = screen;
  ScreenToClient(dialog, &at);

  const std::optional<std::string> text = tipText(dialog, over, at);
  if (!text) {
    if (tipShown)
      TrackCtl_SetToolTip("", 0, 0, true);
    tipShown = false;
    return false;
  }
  TrackCtl_SetToolTip(text->c_str(), screen.x, screen.y + 20, true);
  tipShown = true;
  return true;
}

/// Кисть фона строки состояния, пока она пишет сообщение красным; создаётся
/// при первом сообщении и удаляется вместе с окном.
HBRUSH statusBrush = nullptr;

/// Цвет текста и фон строки состояния `control` для `WM_CTLCOLORSTATIC`:
/// пока она пишет сообщение (`statusAlert`), текст красный, фон — цвета
/// диалога.
///
/// @return кисть фона; пусто — у контрола цвета по умолчанию.
HBRUSH statusColors(HWND dialog, HDC context, HWND control) {
  if (control != GetDlgItem(dialog, IDC_STATUS) || !statusAlert())
    return nullptr;
  // Без своей кисти цвет текста не держится: SWELL на Linux берёт цвета по
  // умолчанию, Windows не красит текст.
  if (!statusBrush)
    statusBrush = CreateSolidBrush(GetSysColor(COLOR_3DFACE));
  SetTextColor(context, nativeColor(render::kBad));
  SetBkColor(context, GetSysColor(COLOR_3DFACE));
  SetBkMode(context, TRANSPARENT);
  return statusBrush;
}

/// Отвечает на `WM_SETCURSOR` окна `dialog` над контролом `over`: над полем с
/// числом и над обрезанной строкой состояния ставит подсказку (`showTip`) и
/// оставляет курсор, который ставит контрол, — над полем курсор ввода текста;
/// над ползунком шкалы ставит курсор «влево-вправо».
///
/// @return результат диалоговой процедуры: 1 — курсор поставлен окном.
INT_PTR onSetCursor(HWND dialog, HWND over) {
  if (showTip(dialog, over))
    return 0;
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

/// Забывает состояние окна, которое уничтожается: перетаскивание, нарисованное,
/// отклонённое поле, обрезанную строку состояния, подсказку у курсора и кисть
/// строки состояния.
void forgetWindowState() {
  components.reset();
  drag.reset();
  drawnCollapsed.reset();
  placedMidi.reset();
  rejection.reset();
  fittedWidth = -1;
  statusCut = false;
  if (tipShown)
    TrackCtl_SetToolTip("", 0, 0, true);
  tipShown = false;
  if (statusBrush)
    DeleteObject(statusBrush);
  statusBrush = nullptr;
}

/// Кончает правку поля с числом при щелчке по окну `dialog` вне контролов —
/// по строкам тактов, шкале окна попадания, свободному месту панели: если
/// фокус ввода в Offset, Tolerance или Silence, окно ставит фокус себе, и поле
/// шлёт `EN_KILLFOCUS` (`onCommand`). Фокус ввода в другом месте остаётся там.
void leaveNumberField(HWND dialog) {
  HWND focus = GetFocus();
  for (const int control : kNumberFields)
    if (focus && focus == GetDlgItem(dialog, control)) {
      SetFocus(dialog);
      return;
    }
}

/// Координата x точки мыши из `lParam` сообщения мыши.
int mouseX(LPARAM lParam) { return static_cast<short>(LOWORD(lParam)); }

/// Координата y точки мыши из `lParam` сообщения мыши.
int mouseY(LPARAM lParam) { return static_cast<short>(HIWORD(lParam)); }

/// Диалоговая процедура окна тренажёра: заполняет контролы при создании,
/// передаёт команды контролов (`onCommand`), рисует шкалу, рамки полей,
/// строки тактов и кнопку панели настроек, ведёт перетаскивание краёв окна
/// попадания мышью, красит строку состояния и закрывает окно.
///
/// @return ответ на сообщение по правилам диалоговой процедуры; для
///   `WM_CTLCOLORSTATIC` — кисть фона, для `WM_SETCURSOR` — ответ
///   `onSetCursor`.
INT_PTR CALLBACK proc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
  case WM_INITDIALOG:
    components.emplace(trainer().settings());
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
    // Ширина окна в доке меняет видимую часть строки состояния.
    showStatus(dialog);
    placeCalibrationButtons(dialog);
    InvalidateRect(dialog, nullptr, FALSE);
    return 0;

  case WM_PAINT: {
    PAINTSTRUCT paint{};
    if (HDC context = BeginPaint(dialog, &paint)) {
      {
        // Холст возвращает контексту прежний шрифт до EndPaint.
        ContextCanvas canvas(context);
        paintGaugeIn(dialog, canvas);
        paintFieldFrames(dialog, canvas);
        paintRowsIn(dialog, canvas);
        paintIndicatorsIn(dialog, canvas);
      }
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
    // Сначала поле с числом отдаёт действующее значение, затем шкала берёт
    // окно попадания.
    leaveNumberField(dialog);
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

  case WM_CTLCOLORSTATIC:
    // Кисть возвращается результатом диалоговой процедуры: так заведено для
    // WM_CTLCOLOR*.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    return reinterpret_cast<INT_PTR>(statusColors(dialog, reinterpret_cast<HDC>(wParam),
                                                  // NOLINTNEXTLINE(performance-no-int-to-ptr)
                                                  reinterpret_cast<HWND>(lParam)));

  case WM_SETCURSOR:
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    return onSetCursor(dialog, reinterpret_cast<HWND>(wParam));

  case WM_CLOSE:
    closeWindow();
    return 1;

  case WM_DESTROY:
    forgetWindowState();
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

  // До WM_INITDIALOG: строка состояния нового окна уже с сообщением о не
  // принятых строках reaper-extstate.ini.
  trainer().fileNotice().onWindowOpen();
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
