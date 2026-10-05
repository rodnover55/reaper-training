# Время ноты, сетка режима, свод нот в строки тактов и модель показа (архив
# add-timing-trainer, design.md D2, D5, D9; design.md D1–D7). От ОС и хоста не
# зависит: это проверяет тест grid_isolation.
add_library(training_grid STATIC
  src/bars.cpp
  src/display.cpp
  src/nodes.cpp
  src/note_time.cpp)

target_include_directories(training_grid PUBLIC include)

# Строки модели показа форматирует {fmt} (design.md D9).
target_link_libraries(training_grid PUBLIC fmt::fmt)

training_enable_warnings(training_grid)
