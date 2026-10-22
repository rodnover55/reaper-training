# Списки изоляции библиотеки training_core_input. Механика — в
# cmake/CheckIsolation.cmake, общая часть списков — в cmake/TrainingIsolation.cmake.

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/TrainingIsolation.cmake")

set(isolation_name "приём сигнала ядром")

set(forbidden_patterns ${training_forbidden_patterns})

set(allowed_includes
  "training/core/"
  ${training_standard_includes})
