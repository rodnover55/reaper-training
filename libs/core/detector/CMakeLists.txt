add_library(training_core_detector STATIC
  src/detector.cpp
  src/filter.cpp)

target_include_directories(training_core_detector PUBLIC include)

target_link_libraries(training_core_detector PUBLIC training_core)

training_enable_warnings(training_core_detector)
