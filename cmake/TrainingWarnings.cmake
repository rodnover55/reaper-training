# Предупреждения компилятора для своих целей.
#
# Чужим исходникам (doctest, {fmt}, SDK и WDL) этот набор не достаётся
# намеренно: чинить там нечего, а их заголовки подключаются как SYSTEM, чтобы
# не шуметь в своей сборке.
#
# Превращать предупреждения в ошибки решает REAPER_TRAINING_WERROR: в пресетах
# он включён, при ручной сборке по умолчанию выключен — недописанный код не
# должен спотыкаться о неиспользованную переменную.

function(training_enable_warnings target)
  target_compile_options(${target} PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -Wcast-align
    -Wdouble-promotion
    -Wformat=2
    -Wimplicit-fallthrough
    -Wconversion
    -Wsign-conversion
    $<$<BOOL:${REAPER_TRAINING_WERROR}>:-Werror>)
endfunction()
