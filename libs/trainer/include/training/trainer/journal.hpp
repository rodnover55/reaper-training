#pragma once

// Журнал ядра тренажёра: что ядро решило и почему.

#include <fmt/format.h>

#include <functional>
#include <string_view>
#include <utility>

namespace training::trainer {

/// Приёмник строк журнала: ядро передаёт в него строку за строкой, без
/// перевода строки в конце. Пустой приёмник строки отбрасывает. Зовётся в том
/// же потоке, что и ядро.
using Journal = std::function<void(std::string_view line)>;

/// Форматирует строку по правилам {fmt} и передаёт её в `journal`; в пустой
/// приёмник ничего не форматирует.
template <class... Args>
void write(const Journal &journal, fmt::format_string<Args...> format, Args &&...args) {
  if (journal)
    journal(fmt::format(format, std::forward<Args>(args)...));
}

} // namespace training::trainer
