#pragma once

// Журнал отладочной сборки: что расширение видит в REAPER и как быстро
// отвечает (design.md D10). В обычной сборке вызовы журнала пусты и ничего не
// стоят.

#include <fmt/format.h>

#include <chrono>
#include <string>
#include <string_view>
#include <utility>

namespace training::reaper {

/// Открывает журнал в файле `path`, дописывая в конец. Повторный вызов при
/// открытом журнале ничего не делает. Зовётся при загрузке расширения из
/// главного потока; этот поток в журнале называется `main`.
void openJournal(const std::string &path);

/// Закрывает журнал. Строки после закрытия никуда не пишутся.
void closeJournal();

/// Пишет строку с отметкой времени от открытия журнала и именем потока. Можно
/// звать из любого потока: строки не перемешиваются. Звуковому потоку журнал
/// не годится — запись в файл ждёт диск; оттуда отметки передаются очередью.
void journalLine(std::string_view line);

/// Время по часам журнала, в секундах от его открытия: для отметок, снятых
/// в одном потоке, а записанных в журнал из другого.
double journalSeconds(std::chrono::steady_clock::time_point at);

/// Форматирует строку по правилам {fmt} и пишет её в журнал. В обычной сборке
/// не делает ничего, даже не форматирует.
template <class... Args> void journal(fmt::format_string<Args...> format, Args &&...args) {
#ifdef TRAINING_DEBUG_BUILD
  journalLine(fmt::format(format, std::forward<Args>(args)...));
#else
  (void)format;
  ((void)args, ...);
#endif
}

} // namespace training::reaper
