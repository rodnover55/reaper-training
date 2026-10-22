#include "training/reaper/context_canvas.hpp"

// Строки в коде — UTF-8, а модуль на Windows собран без UNICODE: DrawText там
// читает текст в кодовой странице системы, и UTF-8 пишет DrawTextUTF8. На
// других ОС SWELL понимает UTF-8 сам, и DrawTextUTF8 — тот же DrawText; там
// заголовку нужен wdltypes.h, который он не подключает сам.
#include <WDL/wdltypes.h>
#include <WDL/win32_utf8.h>

#include <cmath>
#include <cstdint>
#include <string>

namespace training::reaper {
namespace {

/// Округляет координату до пикселя.
int pixel(double value) { return static_cast<int>(std::lround(value)); }

/// Прямоугольник `box` в пикселях.
RECT rectOf(const render::Box &box) {
  return RECT{.left = pixel(box.left),
              .top = pixel(box.top),
              .right = pixel(box.right),
              .bottom = pixel(box.bottom)};
}

/// Цвет для отрисовки по цвету Win32 или SWELL `native`.
render::Color colorOf(int native) {
  return {.red = static_cast<std::uint8_t>(GetRValue(native)),
          .green = static_cast<std::uint8_t>(GetGValue(native)),
          .blue = static_cast<std::uint8_t>(GetBValue(native))};
}

/// Кисть и перо одного цвета, выбранные в контекст на время жизни объекта:
/// ими закрашиваются фигуры с контуром.
class Fill {
public:
  /// Создаёт кисть и перо цвета `color` и выбирает их в контекст `context`,
  /// который живёт дольше объекта.
  Fill(HDC context, int color)
      : context_(context), brush_(CreateSolidBrush(color)),
        pen_(CreatePen(PS_SOLID, 0, color)), previousBrush_(SelectObject(context, brush_)),
        previousPen_(SelectObject(context, pen_)) {}

  /// Возвращает в контекст прежние кисть и перо и удаляет свои.
  ~Fill() {
    SelectObject(context_, previousPen_);
    SelectObject(context_, previousBrush_);
    DeleteObject(pen_);
    DeleteObject(brush_);
  }

  /// Копии нет: объект владеет кистью и пером.
  Fill(const Fill &) = delete;

  /// Присваивания копией нет: объект владеет кистью и пером.
  Fill &operator=(const Fill &) = delete;

  /// Перемещения нет: объект возвращает кисть и перо своему контексту.
  Fill(Fill &&) = delete;

  /// Присваивания перемещением нет: объект возвращает кисть и перо своему
  /// контексту.
  Fill &operator=(Fill &&) = delete;

private:
  HDC context_;
  HBRUSH brush_;
  HPEN pen_;
  HGDIOBJ previousBrush_;
  HGDIOBJ previousPen_;
};

} // namespace

ContextCanvas::ContextCanvas(HDC context, HFONT font) : context_(context), font_(font) {
  SetBkMode(context, TRANSPARENT);
}

ContextCanvas::~ContextCanvas() {
  if (previous_)
    SelectObject(context_, previous_);
  for (const Made &made : fonts_)
    DeleteObject(made.font);
}

void ContextCanvas::fillBox(const render::Box &box, render::Color color) {
  const RECT rect = rectOf(box);
  HBRUSH brush = CreateSolidBrush(nativeColor(color));
  FillRect(context_, &rect, brush);
  DeleteObject(brush);
}

void ContextCanvas::fillEllipse(const render::Box &box, render::Color color) {
  const Fill fill(context_, nativeColor(color));
  const RECT rect = rectOf(box);
  Ellipse(context_, rect.left, rect.top, rect.right, rect.bottom);
}

void ContextCanvas::fillRounded(const render::Box &box, double radius, render::Color color) {
  const Fill fill(context_, nativeColor(color));
  const RECT rect = rectOf(box);
  RoundRect(context_, rect.left, rect.top, rect.right, rect.bottom, pixel(radius * 2.0),
            pixel(radius * 2.0));
}

void ContextCanvas::fillPolygon(std::span<const render::Point> points, render::Color color) {
  std::vector<POINT> corners;
  corners.reserve(points.size());
  for (const render::Point &point : points)
    corners.push_back(POINT{.x = pixel(point.x), .y = pixel(point.y)});
  const Fill fill(context_, nativeColor(color));
  Polygon(context_, corners.data(), static_cast<int>(corners.size()));
}

void ContextCanvas::useFont(double size, bool bold) {
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
  select(font);
}

void ContextCanvas::useContextFont() {
  if (font_)
    select(font_);
  else if (previous_)
    SelectObject(context_, previous_);
}

double ContextCanvas::textWidth(std::string_view text) {
  const std::string copy(text);
  RECT box{.left = 0, .top = 0, .right = 0, .bottom = 0};
  DrawTextUTF8(context_, copy.c_str(), -1, &box,
               DT_LEFT | DT_TOP | DT_SINGLELINE | DT_CALCRECT);
  return static_cast<double>(box.right - box.left);
}

void ContextCanvas::drawText(std::string_view text, const render::Box &box,
                             render::Color color, render::TextAlign align) {
  int flags = DT_SINGLELINE;
  switch (align) {
  case render::TextAlign::TopLeft:
    flags |= DT_LEFT | DT_TOP | DT_NOCLIP;
    break;
  case render::TextAlign::Center:
    flags |= DT_CENTER | DT_VCENTER | DT_NOCLIP;
    break;
  case render::TextAlign::MiddleLeft:
    flags |= DT_LEFT | DT_VCENTER;
    break;
  }
  const std::string copy(text);
  RECT rect = rectOf(box);
  SetTextColor(context_, nativeColor(color));
  DrawTextUTF8(context_, copy.c_str(), -1, &rect, flags);
}

void ContextCanvas::select(HGDIOBJ font) {
  HGDIOBJ previous = SelectObject(context_, font);
  if (!previous_)
    previous_ = previous;
}

int nativeColor(render::Color color) { return RGB(color.red, color.green, color.blue); }

render::DialogColors dialogColors() {
  return {.face = colorOf(GetSysColor(COLOR_3DFACE)),
          .text = colorOf(GetSysColor(COLOR_BTNTEXT)),
          .shadow = colorOf(GetSysColor(COLOR_3DSHADOW)),
          .highlight = colorOf(GetSysColor(COLOR_3DHILIGHT))};
}

render::Box boxOf(const RECT &rect) {
  return {.left = static_cast<double>(rect.left),
          .top = static_cast<double>(rect.top),
          .right = static_cast<double>(rect.right),
          .bottom = static_cast<double>(rect.bottom)};
}

} // namespace training::reaper
