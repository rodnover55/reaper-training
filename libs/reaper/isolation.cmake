# Списки изоляции библиотеки training_reaper. Механика — в
# cmake/CheckIsolation.cmake, общая часть списков — в cmake/TrainingIsolation.cmake.
#
# Связке можно любые слои, кроме src (docs/architecture.md): заголовки слоёв
# лежат под training/, а заголовки src подключаются без него. Ещё разрешены
# SDK REAPER и WDL — через них связка рисует.

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/TrainingIsolation.cmake")

set(isolation_name "связка с REAPER")

set(forbidden_patterns "")

set(allowed_includes
  "training/"
  "reaper_plugin"
  "WDL/"
  ${training_standard_includes})
