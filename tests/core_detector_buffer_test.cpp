#include <doctest/doctest.h>

#include "training/core/detector/buffer.hpp"

#include <optional>
#include <vector>

using training::core::detector::Buffer;

namespace {

/// Забирает все элементы буфера по порядку.
std::vector<int> popAll(Buffer<int> &buffer) {
  std::vector<int> items;
  while (const std::optional<int> item = buffer.pop())
    items.push_back(*item);
  return items;
}

} // namespace

TEST_CASE("Буфер: элементы забираются по порядку добавления") {
  Buffer<int> buffer(4);
  CHECK_FALSE(buffer.pop());

  buffer.push(10);
  buffer.push(20);
  CHECK(buffer.pop() == 10);
  buffer.push(30);
  CHECK(popAll(buffer) == std::vector<int>{20, 30});
  CHECK_FALSE(buffer.pop());
}

TEST_CASE("Буфер: при переполнении выбрасывается самый старый элемент") {
  Buffer<int> buffer(3);
  for (int i = 0; i < 5; ++i)
    buffer.push(i);

  CHECK(popAll(buffer) == std::vector<int>{2, 3, 4});
}

TEST_CASE("Буфер: clear очищает буфер") {
  Buffer<int> buffer(3);
  buffer.push(1);
  buffer.clear();
  CHECK_FALSE(buffer.pop());

  buffer.push(2);
  CHECK(popAll(buffer) == std::vector<int>{2});
}

TEST_CASE("Буфер: буфер на ноль элементов пуст всегда") {
  Buffer<int> buffer(0);
  buffer.push(1);
  CHECK_FALSE(buffer.pop());
}
