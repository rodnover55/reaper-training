#pragma once

// Холст отрисовки (`render::Canvas`) на контексте рисования окна REAPER: Win32
// на Windows, SWELL на других ОС.

// SWELL объявляет max и min макросами, и они ломают стандартную библиотеку.
#define WDL_NO_DEFINE_MINMAX

#include <reaper_plugin.h>

#include "training/render/canvas.hpp"
#include "training/render/palette.hpp"

#include <span>
#include <string_view>
#include <vector>

namespace training::reaper {

/// Холст на контексте рисования окна: примитивы `render::Canvas` — вызовы
/// рисования Win32 или SWELL. Координаты округляются до пикселя. Текст
/// пишется в UTF-8 на прозрачном фоне.
///
/// Шрифты Arial создаются при первом выборе и удаляются вместе с холстом;
/// шрифт, который был в контексте, холст возвращает при удалении.
class ContextCanvas final : public render::Canvas {
public:
  /// Создаёт холст на контексте `context`, который живёт дольше холста, и
  /// ставит в контексте прозрачный фон текста.
  ///
  /// @param font шрифт контекста для `useContextFont`, например шрифт кнопки;
  ///   nullptr — шрифт, выбранный в контексте сейчас.
  explicit ContextCanvas(HDC context, HFONT font = nullptr);

  /// Возвращает в контекст шрифт, который был в нём до первой смены, и
  /// удаляет созданные шрифты.
  ~ContextCanvas() override;

  /// Копии нет: холст владеет созданными шрифтами.
  ContextCanvas(const ContextCanvas &) = delete;

  /// Присваивания копией нет: холст владеет созданными шрифтами.
  ContextCanvas &operator=(const ContextCanvas &) = delete;

  /// Перемещения нет: холст возвращает шрифт своему контексту.
  ContextCanvas(ContextCanvas &&) = delete;

  /// Присваивания перемещением нет: холст возвращает шрифт своему контексту.
  ContextCanvas &operator=(ContextCanvas &&) = delete;

  /// Закрашивает прямоугольник кистью цвета `color`.
  void fillBox(const render::Box &box, render::Color color) override;

  /// Закрашивает эллипс кистью и пером цвета `color`.
  void fillEllipse(const render::Box &box, render::Color color) override;

  /// Закрашивает скруглённый прямоугольник кистью и пером цвета `color`.
  void fillRounded(const render::Box &box, double radius, render::Color color) override;

  /// Закрашивает многоугольник кистью и пером цвета `color`.
  void fillPolygon(std::span<const render::Point> points, render::Color color) override;

  /// Выбирает в контекст шрифт Arial кегля `size`; ячейка шрифта — в 1.15
  /// раза выше цифр, как у Arial.
  void useFont(double size, bool bold) override;

  /// Выбирает в контекст шрифт, данный холсту, а без него — тот, что был в
  /// контексте до первого `useFont`.
  void useContextFont() override;

  /// Ширина строки выбранным в контекст шрифтом.
  double textWidth(std::string_view text) override;

  /// Пишет строку выбранным в контекст шрифтом.
  void drawText(std::string_view text, const render::Box &box, render::Color color,
                render::TextAlign align) override;

private:
  /// Созданный шрифт: высота ячейки, пикселей, жирность и сам шрифт.
  struct Made {
    int height = 0;
    bool bold = true;
    HFONT font = nullptr;
  };

  /// Выбирает в контекст шрифт `font` и запоминает шрифт контекста до
  /// первой смены.
  void select(HGDIOBJ font);

  HDC context_;
  HFONT font_;
  HGDIOBJ previous_ = nullptr;
  std::vector<Made> fonts_;
};

/// Цвет `color` для вызовов Win32 и SWELL.
int nativeColor(render::Color color);

/// Системные цвета диалога для отрисовки: фон, текст кнопок, тень и блик
/// объёмной рамки.
render::DialogColors dialogColors();

/// Прямоугольник `rect` окна для отрисовки.
render::Box boxOf(const RECT &rect);

} // namespace training::reaper
