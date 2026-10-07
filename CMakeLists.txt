cmake_minimum_required(VERSION 3.29)

# macOS 12 — потолок MacBook Pro Early 2015, на котором тренажёр должен
# работать (platform-support). Задаётся до project() — так требует CMake; на
# других ОС ни на что не влияет.
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Наименьшая версия macOS")

project(reaper-training
  VERSION 0.7.0
  DESCRIPTION "REAPER extension: timing trainer"
  LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Модулей C++20 в проекте нет. Поиск их зависимостей CMake иначе включает сам и
# добавляет в команды компиляции ключи gcc, которых не знает clang-tidy.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(REAPER_TRAINING_BUILD_TESTS "Собирать тесты (REAPER для них не нужен)" ON)
option(REAPER_TRAINING_WERROR "Считать предупреждения компилятора ошибками" OFF)
option(REAPER_TRAINING_STATIC_RUNTIME
  "Вкомпоновать рантайм C++ в модуль — для сборки, которая уходит на чужие машины" OFF)

set(REAPER_TRAINING_SANITIZE "" CACHE STRING
  "Санитайзеры списком для -fsanitize, например address,undefined")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(TrainingWarnings)

# Заглушка SWELL для macOS написана на Objective-C++.
if(APPLE)
  enable_language(OBJCXX)
endif()

# --- Windows ------------------------------------------------------------------
# windows.h иначе объявляет min и max макросами — как SWELL без
# WDL_NO_DEFINE_MINMAX. fopen MSVC объявляет устаревшей.
if(WIN32)
  add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

if(MSVC)
  # Исходники в UTF-8 без BOM, а MSVC по умолчанию читает их в кодировке
  # системы. Только для C и C++: компилятор ресурсов этого ключа не знает.
  add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>")

  # Без этого модулю нужен Visual C++ Redistributable, а его у пользователя
  # может не быть: REAPER покажет расширение в списке со статусом "Error".
  if(REAPER_TRAINING_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
endif()

# Чужой код собирается без предупреждений: чинить там нечего.
if(MSVC)
  set(training_no_warnings /w)
else()
  set(training_no_warnings -w)
endif()

# --- Санитайзеры --------------------------------------------------------------
# Флаги ставятся до объявления целей и поэтому достаются всем, включая чужой
# код: ASan видит только то, что инструментировано.
#
# -fno-sanitize-recover=all обязателен: по умолчанию UBSan печатает замечание и
# продолжает, то есть тест проходит при найденной ошибке.
if(REAPER_TRAINING_SANITIZE)
  add_compile_options(
    "-fsanitize=${REAPER_TRAINING_SANITIZE}"
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
    -g)
  add_link_options("-fsanitize=${REAPER_TRAINING_SANITIZE}")
endif()

# --- Зависимости --------------------------------------------------------------
# Всё скачивается в каталог сборки, в репозиторий ничего не попадает. SDK и WDL
# закреплены по коммиту: релизных тегов у них нет.
include(FetchContent)

FetchContent_Declare(reaper_sdk
  URL https://github.com/justinfrankel/reaper-sdk/archive/a98b8bf959f99702fe4aeae045b7434c76188683.tar.gz
  URL_HASH SHA256=55cf1eb71dc20708fc45992b1e7682b7681842963d1da5a6dcd3c9cca56f9152)

FetchContent_Declare(wdl
  URL https://github.com/justinfrankel/WDL/archive/a30bf439e860f674a9fdd9788ab892a3208af2d9.tar.gz
  URL_HASH SHA256=ca87bdfdeddf8c5590ed75d64a980c3bbaf6dcb96f1be170461e7c90c19f0490)

# {fmt} вместо std::format: на macOS 12 системная библиотека C++ дробные числа
# не форматирует, а {fmt} делает это своим кодом (design.md D9). Ставить и
# проверять его нечего, файловые API ОС и модуль C++20 не нужны.
#
# EXCLUDE_FROM_ALL: {fmt} объявляет ещё и обёртку для C, без условия. Так
# собирается только то, с чем линкуются свои цели.
FetchContent_Declare(fmt
  URL https://github.com/fmtlib/fmt/releases/download/12.2.0/fmt-12.2.0.zip
  URL_HASH SHA256=a2f4a8d51178f954e4c339007f77edd76ba0cb2e36f87a48e5a5403d9be5878f
  EXCLUDE_FROM_ALL)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
set(FMT_OS OFF CACHE BOOL "" FORCE)
set(FMT_MODULE OFF CACHE BOOL "" FORCE)
set(FMT_SYSTEM_HEADERS ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(reaper_sdk wdl fmt)

if(REAPER_TRAINING_BUILD_TESTS)
  FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.5.3.tar.gz
    URL_HASH SHA256=174ebc4e769928959614789c5b4e9c3d0a0f81a62bb608756b127bfebfb21331)
  set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
  set(DOCTEST_WITH_MAIN_IN_STATIC_LIB OFF CACHE BOOL "" FORCE)
  # Иначе `cmake --install` полез бы ставить заголовки doctest в /usr/local.
  # Ставится отсюда ровно одно — модуль расширения в UserPlugins.
  set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(doctest)

  # doctest.h включает <ciso646>, а libstdc++ 15 встречает его директивой
  # #warning: с C++20 заголовок нестандартный. Помечаем заголовки doctest
  # системными: вместе с ними замолкает и всё, что они за собой тянут.
  set_target_properties(doctest PROPERTIES
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${doctest_SOURCE_DIR}")
endif()

# reaper_plugin.h подключает swell как "../WDL/swell/swell.h", то есть ждёт WDL
# соседом каталога sdk/. Кладём туда ссылку на распакованный WDL.
if(NOT EXISTS "${reaper_sdk_SOURCE_DIR}/WDL")
  file(CREATE_LINK "${wdl_SOURCE_DIR}/WDL" "${reaper_sdk_SOURCE_DIR}/WDL"
    RESULT link_status SYMBOLIC)
  if(NOT link_status EQUAL 0)
    file(COPY "${wdl_SOURCE_DIR}/WDL" DESTINATION "${reaper_sdk_SOURCE_DIR}")
  endif()
endif()

# --- Библиотеки ---------------------------------------------------------------
# Всё, что решает точность, собирается и проверяется без REAPER и без
# заголовков ОС (design.md D9). Модуль расширения собирает их вместе.
add_subdirectory(libs/concurrency)
add_subdirectory(libs/onset)
add_subdirectory(libs/grid)

# --- Ресурсы форм -------------------------------------------------------------
# Формы описаны в src/views/forms.rc — одним исходником на все платформы. Под
# SWELL из него порождается таблица C++ скриптом WDL; поэтому для сборки нужен
# perl. На Windows тот же файл собирает компилятор ресурсов
# (src/views/forms_win.rc), поэтому шаг ниже только для не-Windows.
#
# Генератор пишет результат рядом с входным файлом, поэтому .rc и ids.h
# сначала копируются в каталог сборки — иначе порождённое насорит в src/.
if(NOT WIN32)
  find_package(Perl REQUIRED)

  set(swell_res_dir "${CMAKE_CURRENT_BINARY_DIR}/swellres")
  set(swell_res_dlg "${swell_res_dir}/forms.rc_mac_dlg")

  add_custom_command(
    OUTPUT "${swell_res_dlg}"
    BYPRODUCTS "${swell_res_dir}/forms.rc_mac_menu"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${swell_res_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/src/views/forms.rc" "${swell_res_dir}/forms.rc"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/src/views/ids.h" "${swell_res_dir}/ids.h"
    COMMAND "${PERL_EXECUTABLE}" "${wdl_SOURCE_DIR}/WDL/swell/swell_resgen.pl"
            "${swell_res_dir}/forms.rc"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/views/forms.rc"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/views/ids.h"
    COMMENT "Порождение таблицы форм SWELL из views/forms.rc"
    VERBATIM)

  add_custom_target(reaper_training_resources DEPENDS "${swell_res_dlg}")
endif()

# --- Плагин -------------------------------------------------------------------
add_library(reaper_training MODULE
  src/main.cpp
  src/actions.cpp
  src/audio_input.cpp
  src/debug_actions.cpp
  src/journal.cpp
  src/midi_inputs.cpp
  src/project_timeline.cpp
  src/settings.cpp
  src/trainer.cpp
  src/window.cpp)

if(WIN32)
  target_sources(reaper_training PRIVATE src/views/forms_win.rc)
else()
  target_sources(reaper_training PRIVATE src/views/resources.cpp)
  add_dependencies(reaper_training reaper_training_resources)
  target_include_directories(reaper_training PRIVATE "${swell_res_dir}")

  # Таблица диалога порождена чужим скриптом: её агрегаты инициализированы не
  # полностью, и компилятор считает это ошибкой при нашем наборе предупреждений.
  set_source_files_properties(src/views/resources.cpp PROPERTIES
    COMPILE_OPTIONS "${training_no_warnings}")
endif()

# Свои заголовки включаются от корня src/.
target_include_directories(reaper_training PRIVATE src)

# Журнал и отладочные действия есть только в отладочной сборке (design.md D10):
# пользователю они не показываются.
target_compile_definitions(reaper_training PRIVATE
  $<$<CONFIG:Debug>:TRAINING_DEBUG_BUILD>)

# Версия и отметка времени сборки, которые расширение печатает при загрузке.
# По ним видно, тот ли модуль в REAPER: не поставленный вовремя плагин выглядит
# как «поведение не изменилось», и отличить это от настоящей ошибки иначе нечем.
#
# Цель обновляется на каждой сборке, поэтому одна единица трансляции
# пересобирается всегда — цена отметки, которая не врёт.
set(training_build_stamp "${CMAKE_BINARY_DIR}/build_stamp.hpp")

add_custom_target(reaper_training_stamp
  BYPRODUCTS "${training_build_stamp}"
  COMMAND "${CMAKE_COMMAND}" -D "OUTPUT=${training_build_stamp}" -D "VERSION=${PROJECT_VERSION}"
    -P "${CMAKE_SOURCE_DIR}/cmake/WriteBuildStamp.cmake"
  COMMENT "Отметка времени сборки")

add_dependencies(reaper_training reaper_training_stamp)
target_include_directories(reaper_training PRIVATE "${CMAKE_BINARY_DIR}")

target_link_libraries(reaper_training PRIVATE
  training_concurrency training_onset training_grid fmt::fmt)

# SYSTEM: SDK и WDL чужие, их предупреждения не наши.
target_include_directories(reaper_training SYSTEM PRIVATE
  "${reaper_sdk_SOURCE_DIR}/sdk"
  "${wdl_SOURCE_DIR}")

training_enable_warnings(reaper_training)

# Неразрешённый символ иначе доезжает до REAPER и виден только как статус
# "Error" в списке расширений — без имени символа и без намёка на причину.
# Флаг переносит эту ошибку на линковку, где видно и то и другое. Линковщики
# macOS и Windows так поступают и сами.
#
# Под санитайзерами он неприменим: там часть символов рантайма остаётся
# неразрешённой намеренно, их подставляет процесс, который модуль загрузит.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT REAPER_TRAINING_SANITIZE)
  target_link_options(reaper_training PRIVATE -Wl,--no-undefined)
endif()

# libstdc++ системы может оказаться старше той, с которой собран модуль, и
# тогда REAPER его не загрузит. Вкомпонованные libstdc++ и libgcc модуль
# прячет (cmake/reaper_training.map): у REAPER и других расширений свои.
if(REAPER_TRAINING_STATIC_RUNTIME AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(training_exports "${CMAKE_CURRENT_SOURCE_DIR}/cmake/reaper_training.map")
  target_link_options(reaper_training PRIVATE
    -static-libstdc++ -static-libgcc
    "LINKER:--exclude-libs,ALL"
    "LINKER:--version-script=${training_exports}")
  set_property(TARGET reaper_training APPEND PROPERTY LINK_DEPENDS "${training_exports}")
endif()

# REAPER ищет в UserPlugins файлы вида reaper_*.so (.dylib на macOS, .dll на
# Windows).
set_target_properties(reaper_training PROPERTIES
  PREFIX ""
  OUTPUT_NAME "reaper_training")

if(APPLE)
  set_target_properties(reaper_training PROPERTIES SUFFIX ".dylib")
endif()

# Реализация SWELL живёт в самом REAPER; модуль получает её через тонкую
# заглушку, поэтому GTK-заголовки для сборки не нужны. На Windows SWELL нет:
# окна там — Win32.
if(NOT WIN32)
  target_compile_definitions(reaper_training PRIVATE SWELL_PROVIDED_BY_APP)
  if(APPLE)
    set(swell_stub "${wdl_SOURCE_DIR}/WDL/swell/swell-modstub.mm")
    target_link_libraries(reaper_training PRIVATE "-framework Cocoa")
  else()
    set(swell_stub "${wdl_SOURCE_DIR}/WDL/swell/swell-modstub-generic.cpp")
  endif()

  target_sources(reaper_training PRIVATE "${swell_stub}")

  # Заглушка чужая и собирается в составе своей цели, то есть общий набор
  # предупреждений достаётся и ей. Снимаем: исправить её мы всё равно не можем.
  set_source_files_properties("${swell_stub}" PROPERTIES
    COMPILE_OPTIONS "${training_no_warnings}")
else()
  # Обёртки WDL, через которые окно передаёт Win32 текст в UTF-8
  # (src/window.cpp). Отдельной библиотекой: так до чужого кода не доходит
  # набор предупреждений модуля, а снять /W4 у одного файла MSVC молча не даёт.
  add_library(training_win32_utf8 STATIC "${wdl_SOURCE_DIR}/WDL/win32_utf8.c")
  target_compile_options(training_win32_utf8 PRIVATE ${training_no_warnings})
  target_link_libraries(reaper_training PRIVATE training_win32_utf8)
endif()

# --- Установка ----------------------------------------------------------------
# Путь из переменной окружения Windows — с обратными косыми чертами, а CMake
# читает их в правилах установки как экранирование.
if(WIN32)
  file(TO_CMAKE_PATH "$ENV{APPDATA}/REAPER" REAPER_RESOURCE_DIR)
elseif(APPLE)
  set(REAPER_RESOURCE_DIR "$ENV{HOME}/Library/Application Support/REAPER")
else()
  set(REAPER_RESOURCE_DIR "$ENV{HOME}/.config/REAPER")
endif()

set(REAPER_USER_PLUGINS "${REAPER_RESOURCE_DIR}/UserPlugins")

install(TARGETS reaper_training LIBRARY DESTINATION "${REAPER_USER_PLUGINS}")

# --- Тесты --------------------------------------------------------------------
if(REAPER_TRAINING_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# --- Установщик ---------------------------------------------------------------
# После тестов: там же заводится проверка сценария установки для Linux, а
# add_test действует только в каталогах после enable_testing().
add_subdirectory(installer)
