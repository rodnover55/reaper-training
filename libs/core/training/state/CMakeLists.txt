add_library(training_core_training_state STATIC
  src/bar.cpp
  src/timeline.cpp)

target_include_directories(training_core_training_state PUBLIC include)

target_link_libraries(training_core_training_state PUBLIC training_core)

training_enable_warnings(training_core_training_state)
