add_library(training_render STATIC
  src/common/arrow.cpp
  src/common/dashes.cpp
  src/common/dot.cpp
  src/common/fill.cpp
  src/common/label.cpp
  src/common/outline.cpp
  src/common/paragraph.cpp
  src/common/plate.cpp
  src/main/bar_row.cpp
  src/main/bar_rows.cpp
  src/main/change_mark.cpp
  src/main/field_frame.cpp
  src/main/field_texts.cpp
  src/main/fit_text.cpp
  src/main/file_notice.cpp
  src/main/gauge_handle.cpp
  src/main/gauge_layout.cpp
  src/main/gauge_ticks.cpp
  src/main/hit_window_gauge.cpp
  src/main/ms_text.cpp
  src/main/node_value.cpp
  src/main/settings_toggle.cpp
  src/main/status_line.cpp
  src/main/view_settings.cpp
  src/calibration/calibration_panel.cpp
  src/calibration/db_text.cpp
  src/calibration/level_scale.cpp
  src/calibration/level_table.cpp
  src/calibration/note_slot.cpp
  src/calibration/note_slots.cpp
  src/calibration/progress_bar.cpp
  src/calibration/steps_row.cpp)

target_include_directories(training_render PUBLIC include)

# Строки тактов показывают такты запуска, панель калибровки — состояние
# калибратора: они из ядра. Тексты для окна форматирует {fmt}.
target_link_libraries(training_render PUBLIC training_core training_core_state
  training_core_training_state training_core_calibration fmt::fmt)

training_enable_warnings(training_render)
