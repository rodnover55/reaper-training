# Решения тренажёра: запуски транспорта, поиск атак в звуке входа, какие атаки
# и ноты MIDI брать в строки тактов, состояние измерения, сессия калибровки.
# Всё, что даёт хост, приходит параметрами. От ОС и хоста не зависит: это
# проверяет тест trainer_isolation.
add_library(training_trainer STATIC
  src/audio_mode.cpp
  src/calibration.cpp
  src/clock.cpp
  src/loop.cpp
  src/measurement.cpp
  src/midi_notes.cpp
  src/onset_search.cpp
  src/settings.cpp
  src/transport.cpp)

target_include_directories(training_trainer PUBLIC include)

# Строки журнала форматирует {fmt}.
target_link_libraries(training_trainer PUBLIC training_grid training_onset fmt::fmt)

training_enable_warnings(training_trainer)
