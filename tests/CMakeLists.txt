# Один исполняемый файл на все библиотеки: тесты быстрые, REAPER им не нужен,
# а дробить их по целям нечем — граница библиотек проверена направлением
# зависимостей и изоляцией.
add_executable(training_tests
  main.cpp
  concurrency_spsc_ring_test.cpp
  onset_sliding_max_test.cpp
  onset_dead_time_test.cpp
  onset_calibration_test.cpp
  onset_detector_test.cpp
  synth.cpp
  synth_test.cpp
  onset_synth_test.cpp
  wav.cpp
  onset_recordings_test.cpp
  grid_bar_display_test.cpp
  grid_bars_test.cpp
  grid_hit_window_test.cpp
  grid_nodes_test.cpp
  grid_note_time_test.cpp
  grid_timeline_test.cpp
  journal_test.cpp
  # Журнал расширения от REAPER не зависит, и его замена файла проверяется
  # здесь же.
  ${CMAKE_SOURCE_DIR}/src/journal.cpp)

target_link_libraries(training_tests PRIVATE
  doctest::doctest
  fmt::fmt
  training_concurrency
  training_onset
  training_grid)

target_include_directories(training_tests PRIVATE . ${CMAKE_SOURCE_DIR}/src)

# Записи для регрессии читаются прямо из исходников: копировать их в каталог
# сборки незачем.
target_compile_definitions(training_tests PRIVATE
  "TRAINING_TEST_DATA_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/data\"")

training_enable_warnings(training_tests)

# doctest.h включает <ciso646>, а libstdc++ 15 отвечает на него директивой
# #warning. Пометка заголовков системными тут не спасает: clang показывает
# #warning и из системных заголовков, а с -Werror это останавливает сборку.
# Гасим на одной цели — только здесь и подключается doctest.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(training_tests PRIVATE "-Wno-#warnings")
endif()

include("${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")
doctest_discover_tests(training_tests)

# Копия процесса REAPER после fork завершается и с загруженным расширением
# (fork_exit_test.cpp). Собранный модуль загружается с поддельным API REAPER.
# Только Linux: там REAPER так запускает программы, и копия, у которой не
# вышел exec, зовёт exit().
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(training_fork_exit_test fork_exit_test.cpp)
  add_dependencies(training_fork_exit_test reaper_training)

  # Без WDL_NO_DEFINE_MINMAX SWELL объявляет min и max макросами.
  target_compile_definitions(training_fork_exit_test PRIVATE
    SWELL_PROVIDED_BY_APP WDL_NO_DEFINE_MINMAX)
  target_include_directories(training_fork_exit_test SYSTEM PRIVATE
    "${reaper_sdk_SOURCE_DIR}/sdk"
    "${wdl_SOURCE_DIR}")
  target_link_libraries(training_fork_exit_test PRIVATE ${CMAKE_DL_LIBS})
  training_enable_warnings(training_fork_exit_test)

  add_test(NAME module_forked_copy_exits
    COMMAND training_fork_exit_test "$<TARGET_FILE:reaper_training>" "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# Изоляция проверяется отдельно и текстом: заголовки ОС и SDK REAPER находятся
# в системных путях без всякой зависимости, и направление зависимостей их не
# ловит (design.md D9). Механика общая, списки — у каждой библиотеки свои и
# лежат в её isolation.cmake.
foreach(isolated concurrency onset grid)
  add_test(NAME ${isolated}_isolation
    COMMAND "${CMAKE_COMMAND}"
      -D "LIBRARY_DIR=${CMAKE_SOURCE_DIR}/libs/${isolated}"
      -P "${CMAKE_SOURCE_DIR}/cmake/CheckIsolation.cmake")

  set_tests_properties(${isolated}_isolation PROPERTIES LABELS "lint")
endforeach()

# --- Проверки исходников ------------------------------------------------------
# Линтеры оформлены тестами CTest, а не отдельными целями: так «прогнать всё»
# остаётся одной командой, а метки позволяют отделить быстрое от долгого.
add_test(NAME no_std_format
  COMMAND "${CMAKE_COMMAND}"
    -D "SOURCE_DIR=${CMAKE_SOURCE_DIR}"
    -P "${CMAKE_SOURCE_DIR}/cmake/CheckNoStdFormat.cmake")

set_tests_properties(no_std_format PROPERTIES LABELS "lint")

find_program(TRAINING_CLANG_FORMAT NAMES clang-format)
find_program(TRAINING_RUN_CLANG_TIDY NAMES run-clang-tidy)

if(TRAINING_CLANG_FORMAT)
  add_test(NAME clang_format
    COMMAND "${CMAKE_COMMAND}"
      -D "SOURCE_DIR=${CMAKE_SOURCE_DIR}"
      -D "CLANG_FORMAT=${TRAINING_CLANG_FORMAT}"
      -P "${CMAKE_SOURCE_DIR}/cmake/CheckFormat.cmake")

  set_tests_properties(clang_format PROPERTIES LABELS "lint")

  file(GLOB_RECURSE training_formattable
    "${CMAKE_SOURCE_DIR}/libs/*.cpp" "${CMAKE_SOURCE_DIR}/libs/*.hpp"
    "${CMAKE_SOURCE_DIR}/src/*.cpp" "${CMAKE_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_SOURCE_DIR}/tests/*.cpp" "${CMAKE_SOURCE_DIR}/tests/*.hpp")

  add_custom_target(format
    COMMAND "${TRAINING_CLANG_FORMAT}" -i ${training_formattable}
    COMMENT "Форматирование исходников по .clang-format")
else()
  message(STATUS "clang-format не найден — проверка форматирования пропущена")
endif()

# clang-tidy дорог: он разбирает и проверяет каждый файл заново вместе со всеми
# заголовками и сам между запусками ничего не помнит. Поэтому он идёт через
# ctcache — кеш результатов по файлу. Ключ — вывод препроцессора, строки
# #include файла и конфигурация clang-tidy; сохраняется только чистый прогон.
# Неизменённый файл не анализируется вовсе, файл с замечаниями — каждый раз
# заново, так что замечание кеш не прячет.
#
# Метка slow остаётся: первый прогон и правка общего заголовка по-прежнему
# занимают минуты. Повседневный прогон — `ctest --preset quick`, полный —
# push-check.
if(TRAINING_RUN_CLANG_TIDY AND CMAKE_EXPORT_COMPILE_COMMANDS)
  include(ProcessorCount)
  ProcessorCount(training_cpu_count)
  if(training_cpu_count EQUAL 0)
    set(training_cpu_count 1)
  endif()

  find_program(TRAINING_CLANG_TIDY NAMES clang-tidy)
  find_package(Python3 COMPONENTS Interpreter)

  set(training_clang_tidy_binary clang-tidy)
  set(training_clang_tidy_environment "")

  if(TRAINING_CLANG_TIDY AND Python3_Interpreter_FOUND)
    # Один скрипт без зависимостей; свой CMakeLists у проекта есть, но
    # конфигурировать нечего — отсюда SOURCE_SUBDIR в никуда.
    FetchContent_Declare(ctcache
      URL https://github.com/matus-chochlik/ctcache/archive/857b14271d3ded5511c2e6a60f2dc20c1e4b5680.tar.gz
      URL_HASH SHA256=1f3a4a39e0c1cacd1eb88e50be386a78ce8fed81ba516c8d88a67c4f85fd82a9
      SOURCE_SUBDIR do-not-configure)
    FetchContent_MakeAvailable(ctcache)

    # Версия анализатора — часть пути к кешу: ctcache её в ключ не берёт, а
    # новая версия вправе найти то, что старая пропустила.
    execute_process(COMMAND "${TRAINING_CLANG_TIDY}" --version
      OUTPUT_VARIABLE training_clang_tidy_version)
    string(REGEX MATCH "version ([0-9.]+)" training_clang_tidy_version
      "${training_clang_tidy_version}")

    # Своя обёртка, а не `clang-tidy` из репозитория ctcache: та подхватывает
    # личные настройки из ~/.config/ctcache.
    set(training_clang_tidy_binary "${CMAKE_BINARY_DIR}/clang-tidy-cached")
    file(WRITE "${training_clang_tidy_binary}"
      "#!/bin/sh\n"
      "exec \"${Python3_EXECUTABLE}\" "
      "\"${ctcache_SOURCE_DIR}/src/ctcache/clang_tidy_cache.py\" "
      "\"${TRAINING_CLANG_TIDY}\" \"$@\"\n")
    file(CHMOD "${training_clang_tidy_binary}" PERMISSIONS
      OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

    set(training_clang_tidy_environment
      "CTCACHE_LOCAL=1"
      "CTCACHE_DIR=${CMAKE_BINARY_DIR}/ctcache/${CMAKE_MATCH_1}")
  else()
    message(STATUS "clang-tidy или Python 3 не найден — clang-tidy идёт без кеша")
  endif()

  add_test(NAME clang_tidy
    COMMAND "${TRAINING_RUN_CLANG_TIDY}"
      -clang-tidy-binary "${training_clang_tidy_binary}"
      -p "${CMAKE_BINARY_DIR}"
      -quiet
      -j ${training_cpu_count}
      "^${CMAKE_SOURCE_DIR}/(libs|src|tests)/")

  set_tests_properties(clang_tidy PROPERTIES
    LABELS "lint;slow"
    TIMEOUT 1800
    ENVIRONMENT "${training_clang_tidy_environment}")
else()
  message(STATUS "run-clang-tidy не найден — статический анализ пропущен")
endif()

# --- Сборка под санитайзерами -------------------------------------------------
# Санитайзерам нужен свой каталог сборки: их флаги с обычной сборкой не
# уживаются. А workflow-пресет CMake умеет ровно одну ступень конфигурации, то
# есть двумя каталогами в одном пресете не распорядиться.
#
# Поэтому вторая сборка оформлена тестом: `cmake --workflow --preset push-check`
# остаётся одной командой на все проверки. В самой санитарной сборке этот тест
# не регистрируется — иначе он вызывал бы сам себя без конца.
if(NOT REAPER_TRAINING_SANITIZE)
  add_test(NAME sanitize_suite
    COMMAND "${CMAKE_COMMAND}" --workflow --preset sanitize
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

  set_tests_properties(sanitize_suite PROPERTIES LABELS "slow" TIMEOUT 1800)
endif()
