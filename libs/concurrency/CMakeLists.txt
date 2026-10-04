# Очереди между потоками без блокировок. Только заголовки; от ОС и хоста не
# зависит (design.md D9): это проверяет тест concurrency_isolation.
add_library(training_concurrency INTERFACE)

target_include_directories(training_concurrency INTERFACE include)
