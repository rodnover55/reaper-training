#include "training/trainer/fit_text.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace training::trainer {
namespace {

/// Правда, если байт UTF-8 продолжает знак, а не начинает его.
bool continues(char byte) { return (static_cast<unsigned char>(byte) & 0xC0U) == 0x80U; }

} // namespace

std::string fitText(std::string_view text, double width, const TextMeasure &measure) {
  if (measure(text) <= width)
    return std::string(text);

  // Начало укорачивается на знак с конца, пока с отметкой обрезки не
  // поместится: обрезка редка, и знаков отрезается немного.
  std::size_t end = text.size();
  while (end > 0) {
    do
      --end;
    while (end > 0 && continues(text[end]));
    std::string cut = std::string(text.substr(0, end)) + std::string(kCutMark);
    if (measure(cut) <= width)
      return cut;
  }
  return std::string(kCutMark);
}

} // namespace training::trainer
