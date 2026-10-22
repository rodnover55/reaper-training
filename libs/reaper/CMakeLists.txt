add_library(training_reaper STATIC
  src/calibration.cpp
  src/compensation.cpp
  src/context_canvas.cpp
  src/count_in.cpp
  src/half_ms.cpp
  src/input_state.cpp
  src/midi_event.cpp
  src/midi_places.cpp
  src/run_timeline.cpp)

target_include_directories(training_reaper PUBLIC include)

# SYSTEM: SDK и WDL чужие, их предупреждения не наши. Заголовок холста
# подключает reaper_plugin.h, поэтому пути нужны и тем, кто его включает.
target_include_directories(training_reaper SYSTEM PUBLIC
  "${reaper_sdk_SOURCE_DIR}/sdk"
  "${wdl_SOURCE_DIR}")

# Холст рисует примитивы отрисовки, выбранный вход отдаёт строке состояния,
# есть ли он, такты запуска и ноты MIDI идут в ядро тактами и нотами ядра, а
# сессия калибровки держит калибратор ядра.
target_link_libraries(training_reaper PUBLIC
  training_core training_core_input training_core_detector training_core_state
  training_core_training_state training_core_calibration training_core_transport training_render)

if(WIN32)
  # Обёртки WDL, через которые холст и окно передают Win32 текст в UTF-8.
  # Отдельной библиотекой: так до чужого кода не доходит набор предупреждений,
  # а снять /W4 у одного файла MSVC молча не даёт.
  add_library(training_win32_utf8 STATIC "${wdl_SOURCE_DIR}/WDL/win32_utf8.c")
  target_compile_options(training_win32_utf8 PRIVATE ${training_no_warnings})
  target_link_libraries(training_reaper PUBLIC training_win32_utf8)
else()
  # Реализация SWELL живёт в самом REAPER: функции приходят указателями через
  # заглушку, которую собирает модуль расширения.
  target_compile_definitions(training_reaper PUBLIC SWELL_PROVIDED_BY_APP)
endif()

training_enable_warnings(training_reaper)
