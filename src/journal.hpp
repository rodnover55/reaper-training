#pragma once

// Журнал расширения: что расширение видит в REAPER и как быстро отвечает
// (design.md D10). Пишется в любой сборке (`platform-support`).

#include <fmt/format.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace training::reaper {

/// Наибольший размер журнала при открытии, байт: больший файл уходит в `.1`.
inline constexpr std::uintmax_t kJournalLimit = std::uintmax_t{5} * 1024 * 1024;

/// Открывает журнал в файле `path`, дописывая в конец. Если файл больше
/// `kJournalLimit`, он сначала переименовывается в `path` с `.1` в конце —
/// прежний такой файл пропадает, — и журнал начинается заново. Повторный
/// вызов при открытом журнале ничего не делает. Файл, который не открылся,
/// журнал молча не пишет. Зовётся при загрузке расширения из главного потока;
/// этот поток в журнале называется `main`.
///
/// @param path путь в UTF-8, как его отдаёт REAPER.
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

/// Форматирует строку по правилам {fmt} и пишет её в журнал (`journalLine`).
template <class... Args> void journal(fmt::format_string<Args...> format, Args &&...args) {
  journalLine(fmt::format(format, std::forward<Args>(args)...));
}

} // namespace training::reaper
