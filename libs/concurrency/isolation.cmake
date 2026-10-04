# Списки изоляции библиотеки training_concurrency (design.md D9). Механика — в
# cmake/CheckIsolation.cmake, общая часть списков — в cmake/TrainingIsolation.cmake.

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/TrainingIsolation.cmake")

set(isolation_name "очереди")

set(forbidden_patterns ${training_forbidden_patterns})

set(allowed_includes
  "training/concurrency/"
  ${training_standard_includes})
