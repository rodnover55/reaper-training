# Списки изоляции библиотеки training_trainer. Механика — в
# cmake/CheckIsolation.cmake, общая часть списков — в cmake/TrainingIsolation.cmake.
#
# Разрешены сетка и поиск атаки — ядро тренажёра стоит на них — и {fmt}: строки
# журнала форматирует только он.

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/TrainingIsolation.cmake")

set(isolation_name "тренажёр")

set(forbidden_patterns ${training_forbidden_patterns})

set(allowed_includes
  "training/trainer/"
  "training/grid/"
  "training/onset/"
  "fmt/"
  ${training_standard_includes})
