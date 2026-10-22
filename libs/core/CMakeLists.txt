add_library(training_core STATIC
  src/hit_window.cpp
  src/settings.cpp)

target_include_directories(training_core PUBLIC include)

training_enable_warnings(training_core)
