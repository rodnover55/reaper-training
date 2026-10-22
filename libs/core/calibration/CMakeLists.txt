add_library(training_core_calibration STATIC
  src/calibrator.cpp
  src/notes_step.cpp
  src/result_step.cpp
  src/silence_step.cpp)

target_include_directories(training_core_calibration PUBLIC include)

target_link_libraries(training_core_calibration PUBLIC
  training_core training_core_detector training_core_input)

training_enable_warnings(training_core_calibration)
