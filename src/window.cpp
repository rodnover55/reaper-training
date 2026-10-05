#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_GetMainHwnd
#define REAPERAPI_WANT_DockWindowAddEx
#define REAPERAPI_WANT_DockWindowRemove
#define REAPERAPI_WANT_DockWindowActivate
#define REAPERAPI_WANT_GetNumAudioInputs
#define REAPERAPI_WANT_GetInputChannelName

#include "window.hpp"

#include <reaper_plugin_functions.h>

#include "journal.hpp"
#include "trainer.hpp"
#include "views/ids.h"

#include "training/grid/display.hpp"

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

std::string statusText() {
  const Trainer &trainerRef = trainer();
  const Status &status = trainerRef.status();
  const Settings &settings = trainerRef.settings();

  std::string text =
      fmt::format("{}, tolerance {} ms, latency {:.1f} ms",
                  kModeNames[static_cast<std::size_t>(modeIndex(settings.mode))],
                  static_cast<int>(settings.toleranceMs), status.compensationMs);
  if (status.rate != 1.0)
    text += fmt::format(", rate {}", status.rate);

  switch (status.measuring) {
  case Measuring::Waiting:
    text += " — waiting for playback";
    break;
  case Measuring::Running:
    text += " — measuring";
    break;
  case Measuring::NoChannel:
    text += " — selected input is not available";
    break;
  }
  return text;
}

void fillControls(HWND dialog) {
  showingSettings = true;
  const Settings &settings = trainer().settings();

  SendDlgItemMessage(dialog, IDC_MODE, CB_SETCURSEL,
                     static_cast<WPARAM>(modeIndex(settings.mode)), 0);
  SetDlgItemText(dialog, IDC_TOLERANCE,
                 fmt::format("{}", static_cast<int>(settings.toleranceMs)).c_str());
  SetDlgItemText(dialog, IDC_SILENCE,
                 fmt::format("{}", static_cast<int>(settings.silenceDb)).c_str());
  CheckDlgButton(dialog, IDC_SHOW_NUMBERS,
                 settings.showBarNumbers ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(dialog, IDC_SHOW_STATS, settings.showBarStats ? BST_CHECKED : BST_UNCHECKED);

  // Каналы — заново при каждом показе: звуковую карту могли сменить.
  SendDlgItemMessage(dialog, IDC_CHANNEL, CB_RESETCONTENT, 0, 0);
  const int channels = GetNumAudioInputs();
  for (int i = 0; i < channels; ++i) {
    const char *name = GetInputChannelName(i);
    const std::string label = fmt::format("{}: {}", i + 1, name ? name : "");
    SendDlgItemMessage(dialog, IDC_CHANNEL, CB_ADDSTRING, 0,
                       reinterpret_cast<LPARAM>(label.c_str()));
  }
  SendDlgItemMessage(dialog, IDC_CHANNEL, CB_SETCURSEL,
                     static_cast<WPARAM>(settings.channel < channels ? settings.channel : -1),
                     0);

  SetDlgItemText(dialog, IDC_STATUS, statusText().c_str());
  showingSettings = false;
}

void onCommand(HWND dialog, int control, int notification) {
  if (showingSettings)
    return;

  Settings settings = trainer().settings();
  if (notification == CBN_SELCHANGE && control == IDC_MODE) {
    const auto index = SendDlgItemMessage(dialog, IDC_MODE, CB_GETCURSEL, 0, 0);
    if (index < 0 || index >= static_cast<LRESULT>(kModes.size()))
      return;
    settings.mode = kModes[static_cast<std::size_t>(index)];
  } else if (notification == CBN_SELCHANGE && control == IDC_CHANNEL) {
    const auto index = SendDlgItemMessage(dialog, IDC_CHANNEL, CB_GETCURSEL, 0, 0);
    if (index < 0)
      return;
    settings.channel = static_cast<int>(index);
  } else if (notification == EN_CHANGE && control == IDC_TOLERANCE) {
    // Недописанное число настройки не трогает; вне границ — прижимается.
    const auto value = editValue(dialog, IDC_TOLERANCE);
    if (!value)
      return;
    settings.toleranceMs = *value;
  } else if (notification == EN_CHANGE && control == IDC_SILENCE) {
    const auto value = editValue(dialog, IDC_SILENCE);
    if (!value)
      return;
    settings.silenceDb = *value;
  } else if (notification == BN_CLICKED && control == IDC_SHOW_NUMBERS) {
    settings.showBarNumbers = IsDlgButtonChecked(dialog, IDC_SHOW_NUMBERS) == BST_CHECKED;
  } else if (notification == BN_CLICKED && control == IDC_SHOW_STATS) {
    settings.showBarStats = IsDlgButtonChecked(dialog, IDC_SHOW_STATS) == BST_CHECKED;
  } else {
    return;
  }

  trainer().setSettings(settings);
  journal("settings: mode {}, tolerance {} ms, channel {}, silence {} dBFS, bar numbers {}, "
          "mean/spread {}",
          grid::divisions(trainer().settings().mode), trainer().settings().toleranceMs,
          trainer().settings().channel + 1, trainer().settings().silenceDb,
          trainer().settings().showBarNumbers, trainer().settings().showBarStats);
  refreshWindow();
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

int colorOf(grid::Tone tone) {
  switch (tone) {
  case grid::Tone::Good:
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

/// Текст с центром в (`x`, `y`); вылезать за поле `halfWidth` по сторонам ему
/// можно.
void drawCentered(HDC context, const std::string &text, int color, double x, double y,
                  double halfWidth, double halfHeight) {
  RECT box{.left = pixel(x - halfWidth),
           .top = pixel(y - halfHeight),
           .right = pixel(x + halfWidth),
           .bottom = pixel(y + halfHeight)};
  SetTextColor(context, color);
  DrawText(context, text.c_str(), -1, &box,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
}

/// Жирные шрифты Arial одного рисования по кеглю: создаются при первом
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
    for (const auto &[height, font] : fonts_)
      DeleteObject(font);
  }

  /// Выбирает в контекст шрифт кегля `size`, пикселей, — высоты цифр, как
  /// `font-size` макета.
  void use(double size) {
    // Высота ячейки шрифта больше высоты знаков: у Arial — примерно в 1.15 раза.
    const int height = pixel(size * 1.15);
    HFONT font = nullptr;
    for (const auto &[known, made] : fonts_)
      if (known == height)
        font = made;
    if (!font) {
      font = CreateFont(height, 0, 0, 0, FW_BOLD, 0, 0, 0, 0, 0, 0, 0, 0, "Arial");
      fonts_.emplace_back(height, font);
    }
    HGDIOBJ previous = SelectObject(context_, font);
    if (!previous_)
      previous_ = previous;
  }

private:
  HDC context_;
  HGDIOBJ previous_ = nullptr;
  std::vector<std::pair<int, HFONT>> fonts_;
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
  const double big = bigFor(step);
  const double small = std::max(kMinFont - 1.0, std::min(big * 0.6, step / division / 2.0));
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
    DrawText(context, mark.text.c_str(), -1, &box,
             DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOCLIP);
  }

  if (layout.labelWidth > 0.0) {
    fonts.use(layout.sideSize);
    drawCentered(context, row.label, kNeutral, layout.left + layout.labelWidth / 2.0, middle,
                 layout.labelWidth / 2.0, layout.rowHeight / 2.0);
  }

  for (const int beat : row.dots) {
    const bool current = row.currentBeat == beat;
    drawDot(context, x(beat), middle, std::max(4.0, big * (current ? 0.3 : 0.2)),
            current ? kCurrentBeat : kNeutral);
  }

  for (const grid::ValueView &value : row.values) {
    fonts.use(value.onBeat ? big : small);
    drawCentered(context, value.cell.text, colorOf(value.cell.tone), x(value.beat), middle,
                 step, layout.rowHeight / 2.0);
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

/// Рисует строки тактов в области `area` контекста (design.md D8). Размеры —
/// доли высоты строки, так что окно одинаково на любом мониторе.
void paintArea(HDC context, const RECT &area) {
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

/// Рисует строки тактов под контролами окна.
void paintRows(HWND dialog, HDC context) {
  RECT client{};
  GetClientRect(dialog, &client);

  RECT controls{};
  GetWindowRect(GetDlgItem(dialog, IDC_SILENCE), &controls);
  POINT bottom{.x = controls.left, .y = controls.bottom};
  ScreenToClient(dialog, &bottom);

  RECT area = client;
  area.top = bottom.y + 6;
  if (area.bottom > area.top)
    paintArea(context, area);
}

INT_PTR CALLBACK proc(HWND dialog, UINT message, WPARAM wParam, LPARAM /*lParam*/) {
  switch (message) {
  case WM_INITDIALOG:
    for (const char *name : kModeNames)
      SendDlgItemMessage(dialog, IDC_MODE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    fillControls(dialog);
    return 1;

  case WM_COMMAND:
    onCommand(dialog, LOWORD(wParam), HIWORD(wParam));
    return 0;

  case WM_SIZE:
    InvalidateRect(dialog, nullptr, FALSE);
    return 0;

  case WM_PAINT: {
    PAINTSTRUCT paint{};
    if (HDC context = BeginPaint(dialog, &paint)) {
      paintRows(dialog, context);
      EndPaint(dialog, &paint);
    }
    return 1;
  }

  case WM_CLOSE:
    closeWindow();
    return 1;

  case WM_DESTROY:
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

  SetDlgItemText(window, IDC_STATUS, statusText().c_str());
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
#endif

#if defined(TRAINING_DEBUG_BUILD) && !defined(_WIN32)
bool snapshotRows(const char *path, int width, int height) {
  if (!path || width <= 0 || height <= 0)
    return false;

  HDC context = SWELL_CreateMemContext(nullptr, width, height);
  if (!context)
    return false;
  paintArea(context, RECT{.left = 0, .top = 0, .right = width, .bottom = height});

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
#endif

void closeWindow() {
  if (!window)
    return;

  HWND closing = window;
  window = nullptr;
  DockWindowRemove(closing);
  DestroyWindow(closing);
  journal("window: closed");
}

} // namespace training::reaper
