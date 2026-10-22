#pragma once

// Интерфейс отрисовки примитивов: через него компоненты окна (`Element`)
// рисуют себя.

#include <cstdint>
#include <span>
#include <string_view>

namespace training::render {

/// Цвет: доли красного, зелёного и синего, от 0 до 255.
struct Color {
  std::uint8_t red = 0;
  std::uint8_t green = 0;
  std::uint8_t blue = 0;

  /// Правда, если у цветов совпадают все три доли.
  friend bool operator==(const Color &, const Color &) = default;
};

/// Точка, пикселей от левого верхнего угла холста.
struct Point {
  double x = 0.0;
  double y = 0.0;
};

/// Прямоугольник: края по горизонтали и вертикали, пикселей от левого
/// верхнего угла холста; правый и нижний края в прямоугольник не входят.
struct Box {
  double left = 0.0;
  double top = 0.0;
  double right = 0.0;
  double bottom = 0.0;
};

/// Как текст стоит в прямоугольнике.
enum class TextAlign {
  /// Левый верхний угол текста — в левом верхнем углу прямоугольника; правый
  /// и нижний края не важны, текст за них выходит.
  TopLeft,
  /// Текст по центру прямоугольника; за края ему выходить можно.
  Center,
  /// Текст у левого края, по середине высоты; что не помещается в
  /// прямоугольник, обрезается.
  MiddleLeft,
};

/// Холст: примитивы, из которых компоненты окна складывают себя. Координаты
/// дробные, реализация округляет их до пикселя.
///
/// Реализация в модуле расширения рисует в контексте рисования окна, в тестах
/// — может запоминать вызовы.
class Canvas {
public:
  /// Позволяет удалять реализацию через указатель на `Canvas`.
  virtual ~Canvas() = default;

  /// Создаёт холст; своих данных у `Canvas` нет, их хранит реализация.
  Canvas() = default;

  /// Копии нет: реализация держит контекст рисования.
  Canvas(const Canvas &) = delete;

  /// Присваивания копией нет: реализация держит контекст рисования.
  Canvas &operator=(const Canvas &) = delete;

  /// Перемещения нет: реализация держит контекст рисования.
  Canvas(Canvas &&) = delete;

  /// Присваивания перемещением нет: реализация держит контекст рисования.
  Canvas &operator=(Canvas &&) = delete;

  /// Закрашивает прямоугольник `box` цветом `color`.
  virtual void fillBox(const Box &box, Color color) = 0;

  /// Закрашивает эллипс, вписанный в прямоугольник `box`, цветом `color`:
  /// у квадрата — круг.
  virtual void fillEllipse(const Box &box, Color color) = 0;

  /// Закрашивает прямоугольник `box` со скруглёнными углами радиуса `radius`
  /// цветом `color`.
  virtual void fillRounded(const Box &box, double radius, Color color) = 0;

  /// Закрашивает многоугольник с вершинами `points` по порядку цветом
  /// `color`.
  virtual void fillPolygon(std::span<const Point> points, Color color) = 0;

  /// Выбирает для текста шрифт Arial кегля `size`, пикселей: `size` — высота
  /// цифр, ячейка шрифта выше; жирный, если `bold`.
  virtual void useFont(double size, bool bold) = 0;

  /// Выбирает для текста шрифт контекста: тот, что дала холсту реализация,
  /// например шрифт кнопки.
  virtual void useContextFont() = 0;

  /// Ширина строки `text` в одну строку выбранным шрифтом, пикселей.
  virtual double textWidth(std::string_view text) = 0;

  /// Пишет строку `text` в одну строку выбранным шрифтом цветом `color` в
  /// прямоугольнике `box` по выравниванию `align`. Фон под текстом не
  /// закрашивается.
  virtual void drawText(std::string_view text, const Box &box, Color color,
                        TextAlign align) = 0;
};

} // namespace training::render
