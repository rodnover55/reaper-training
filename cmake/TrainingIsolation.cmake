# Общая часть списков изоляции: то, что запрещено во всех независимых
# библиотеках. Подключается из `libs/<имя>/isolation.cmake`,
# а те дописывают своё.

# Понятия хоста, ОС и их библиотек. Заголовки ловит список разрешённых
# включений; здесь — то, что пролезает мимо него: имена API в коде и ветки под
# конкретную ОС.
set(training_forbidden_patterns
  "[Rr][Ee][Aa][Pp][Ee][Rr]"
  "PCM_"
  "ReaSample"
  "MediaTrack"
  "TimeMap"
  "HWND"
  "SWELL"
  "LICE"
  "clock_gettime"
  "QueryPerformance"
  "mach_absolute_time"
  "_WIN32"
  "__APPLE__"
  "__linux__")

# Стандартная библиотека. Заголовков C из неё (<unistd.h> и прочих POSIX)
# в списке нет намеренно: это уже ОС. <format> нет тоже: строки форматирует
# {fmt}.
set(training_standard_includes
  "algorithm" "array" "atomic" "bit" "cassert" "cctype" "charconv" "chrono" "cmath"
  "compare" "concepts" "cstddef" "cstdint" "cstring" "deque" "expected" "functional"
  "iterator" "limits" "map" "memory" "mutex" "numbers" "numeric" "optional" "ranges" "span"
  "stdexcept" "string" "string_view" "type_traits" "utility" "vector")
