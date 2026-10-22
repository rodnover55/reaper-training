add_library(training_core_training STATIC
  src/audio.cpp
  src/midi.cpp
  src/trainer.cpp)

target_include_directories(training_core_training PUBLIC include)

target_link_libraries(training_core_training PUBLIC
  training_core training_core_input training_core_detector training_core_training_state)

training_enable_warnings(training_core_training)
