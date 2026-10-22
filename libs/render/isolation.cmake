# Списки изоляции библиотеки training_render. Механика — в
# cmake/CheckIsolation.cmake, общая часть списков — в cmake/TrainingIsolation.cmake.
#
# Разрешены только ядро (docs/architecture.md) — libs/core, из которого
# компоненты берут то, что показывают, — и {fmt}: тексты для окна
# форматирует только он.

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/TrainingIsolation.cmake")

set(isolation_name "отрисовка")

set(forbidden_patterns ${training_forbidden_patterns})

set(allowed_includes
  "training/render/"
  "training/core/"
  "fmt/"
  ${training_standard_includes})
