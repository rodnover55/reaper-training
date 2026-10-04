# Списки изоляции библиотеки training_grid (design.md D9). Механика — в
# cmake/CheckIsolation.cmake, общая часть списков — в cmake/TrainingIsolation.cmake.
#
# {fmt} разрешён: модель показа готовит строки для окна, а форматирует их
# только {fmt}.

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/TrainingIsolation.cmake")

set(isolation_name "сетка")

set(forbidden_patterns ${training_forbidden_patterns})

set(allowed_includes
  "training/grid/"
  "fmt/"
  ${training_standard_includes})
