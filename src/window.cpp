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
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
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
  } else {
    return;
  }

  trainer().setSettings(settings);
  journal("settings: mode {}, tolerance {} ms, channel {}, silence {} dBFS",
          grid::divisions(trainer().settings().mode), trainer().settings().toleranceMs,
          trainer().settings().channel + 1, trainer().settings().silenceDb);
  refreshWindow();
}

int colorOf(grid::Tone tone) {
  switch (tone) {
  case grid::Tone::Good:
    return RGB(90, 210, 90);
  case grid::Tone::Bad:
    return RGB(235, 80, 70);
  case grid::Tone::Neutral:
    break;
  }
  return RGB(170, 170, 170);
}

void drawText(HDC context, const std::string &text, int color, RECT box) {
  SetTextColor(context, color);
  DrawText(context, text.c_str(), -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

/// Рисует строки ударов под контролами: новые сверху, кегль от высоты окна.
void paintRows(HWND dialog, HDC context) {
  RECT client{};
  GetClientRect(dialog, &client);

  RECT controls{};
  GetWindowRect(GetDlgItem(dialog, IDC_SILENCE), &controls);
  POINT bottom{.x = controls.left, .y = controls.bottom};
  ScreenToClient(dialog, &bottom);

  RECT area = client;
  area.top = bottom.y + 6;
  if (area.bottom <= area.top)
    return;

  HBRUSH background = CreateSolidBrush(RGB(24, 24, 24));
  FillRect(context, &area, background);
  DeleteObject(background);

  const std::vector<grid::RowView> rows =
      grid::present(trainer().rows(), trainer().settings().toleranceMs);

  // Столбцы: подпись, узлы удара, среднее и разброс.
  auto cells = static_cast<std::size_t>(grid::divisions(trainer().settings().mode));
  for (const grid::RowView &row : rows)
    cells = std::max(cells, row.cells.size());
  const std::size_t columns = 1 + cells + (cells > 1 ? 2 : 0);

  constexpr int kRows = 8;
  const int width = static_cast<int>(area.right - area.left);
  const int rowHeight = static_cast<int>(area.bottom - area.top) / kRows;
  const int columnWidth = width / static_cast<int>(columns);
  const int fontHeight = std::clamp(std::min(rowHeight * 7 / 10, columnWidth / 3), 10, 160);

  HFONT font = CreateFont(fontHeight, 0, 0, 0, FW_BOLD, 0, 0, 0, 0, 0, 0, 0, 0, "Arial");
  HGDIOBJ previous = SelectObject(context, font);
  SetBkMode(context, TRANSPARENT);

  for (std::size_t r = 0; r < rows.size() && r < kRows; ++r) {
    const grid::RowView &row = rows[r];
    RECT box{.left = area.left,
             .top = area.top + static_cast<int>(r) * rowHeight,
             .right = area.left + columnWidth,
             .bottom = area.top + (static_cast<int>(r) + 1) * rowHeight};

    drawText(context, row.label, colorOf(grid::Tone::Neutral), box);

    for (const grid::Cell &cell : row.cells) {
      box.left += columnWidth;
      box.right += columnWidth;
      drawText(context, cell.text, colorOf(cell.tone), box);
    }

    if (row.mean) {
      box.left = area.left + static_cast<int>(1 + cells) * columnWidth;
      box.right = box.left + columnWidth;
      drawText(context, row.mean->text, colorOf(row.mean->tone), box);
    }

    if (row.spread) {
      box.left = area.left + static_cast<int>(2 + cells) * columnWidth;
      box.right = box.left + columnWidth;
      drawText(context, row.spread->text, colorOf(row.spread->tone), box);
    }
  }

  SelectObject(context, previous);
  DeleteObject(font);
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
