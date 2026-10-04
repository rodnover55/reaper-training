# Проверка, что строки форматирует только {fmt} (design.md D9).
#
# std::format с дробными числами на macOS 12 не собирается: системная
# библиотека C++ умеет это только с macOS 13.3. Ошибку покажет сборка на macOS
# в CI, но не сборка на Linux, где идёт разработка, — поэтому проверка здесь,
# текстом и на любой ОС.
#
# Список файлов собирается на запуске теста, а не на конфигурации: иначе новый
# файл остался бы непроверенным до следующего cmake.

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR не задан")
endif()

file(GLOB_RECURSE sources
  "${SOURCE_DIR}/libs/*.cpp" "${SOURCE_DIR}/libs/*.hpp"
  "${SOURCE_DIR}/src/*.cpp" "${SOURCE_DIR}/src/*.hpp"
  "${SOURCE_DIR}/tests/*.cpp" "${SOURCE_DIR}/tests/*.hpp")

set(failures "")

foreach(source IN LISTS sources)
  file(READ "${source}" text)
  file(RELATIVE_PATH shown "${SOURCE_DIR}" "${source}")

  foreach(pattern "#include[ \t]*<format>" "#include[ \t]*<print>" "std::format"
                  "std::vformat" "std::print")
    string(REGEX MATCH "${pattern}" hit "${text}")
    if(NOT hit STREQUAL "")
      list(APPEND failures "${shown}: встретилось \"${hit}\"")
    endif()
  endforeach()
endforeach()

if(NOT failures STREQUAL "")
  list(JOIN failures "\n  " report)
  message(FATAL_ERROR "вместо {fmt} использован std::format или std::print:\n  ${report}")
endif()

list(LENGTH sources count)
message(STATUS "std::format не используется: проверено файлов — ${count}")
