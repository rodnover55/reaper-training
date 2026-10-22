#pragma once

// Кадр отрисовки: куда и в каком месте рисует компонент.

#include "training/render/canvas.hpp"

namespace training::render {

/// То, что компонент получает на каждое рисование: холст и отведённое ему
/// место. Всё остальное, что он показывает, — его состояние.
struct Frame {
  /// Куда рисовать.
  Canvas &canvas;

  /// Место на холсте, отведённое компоненту. Компонент раскладывает себя по
  /// нему: размеры, кегли и места детей — доли этого места.
  Box area;

  /// Тот же кадр с другим местом — его родитель отводит ребёнку.
  Frame within(const Box &place) const { return {.canvas = canvas, .area = place}; }
};

} // namespace training::render
