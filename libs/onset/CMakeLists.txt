# Поиск атаки в потоке сэмплов (design.md D4). От ОС и хоста не зависит
# (design.md D9): это проверяет тест onset_isolation.
add_library(training_onset STATIC
  src/detector.cpp)

target_include_directories(training_onset PUBLIC include)

training_enable_warnings(training_onset)
