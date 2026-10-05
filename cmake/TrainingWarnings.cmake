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
  if(MSVC)
    # C4324 — «структура дополнена из-за alignas»: в SpscRing ради этого
    # alignas и стоит. Компилятору ресурсов ключи C++ не передаются: он их
    # не знает.
    target_compile_options(${target} PRIVATE
      "$<$<COMPILE_LANGUAGE:C,CXX>:/W4;/wd4324;$<$<BOOL:${REAPER_TRAINING_WERROR}>:/WX>>")
  else()
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
  endif()
endfunction()
