#include <doctest/doctest.h>

#include "training/concurrency/spsc_ring.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <thread>
#include <vector>

using training::concurrency::SpscRing;

TEST_CASE("очередь: порядок, заполнение и отброс лишнего") {
  SpscRing<int, 4> ring;
  CHECK_FALSE(ring.pop());
  CHECK(ring.freeSpace() == 4);

  for (int i = 0; i < 4; ++i)
    CHECK(ring.push(i));
  CHECK(ring.freeSpace() == 0);
  CHECK_FALSE(ring.push(4)); // места нет — запись отброшена, а не ждёт
  CHECK(ring.dropped() == 1);

  for (int i = 0; i < 4; ++i)
    CHECK(ring.pop() == i);
  CHECK_FALSE(ring.pop());

  // Индексы идут дальше ёмкости — буфер оборачивается.
  for (int round = 0; round < 10; ++round) {
    CHECK(ring.push(round));
    CHECK(ring.pop() == round);
  }
}

TEST_CASE("очередь: пачка кладётся целиком или не кладётся вовсе") {
  SpscRing<float, 8> ring;
  const std::array<float, 5> first{1, 2, 3, 4, 5};
  const std::array<float, 4> second{6, 7, 8, 9};

  CHECK(ring.push(std::span<const float>(first)));
  CHECK(ring.freeSpace() == 3);

  // На вторую пачку места не хватает: ни одного элемента из неё нет.
  CHECK_FALSE(ring.push(std::span<const float>(second)));
  CHECK(ring.dropped() == 1);
  CHECK(ring.freeSpace() == 3);

  CHECK(ring.push(std::span<const float>()));

  std::array<float, 3> out{-1, -1, -1};
  CHECK(ring.pop(std::span<float>(out)) == 3);
  CHECK(out == std::array<float, 3>{1, 2, 3});

  // Пачка через край буфера: место есть, индексы оборачиваются.
  CHECK(ring.push(std::span<const float>(second)));

  std::array<float, 10> rest{};
  rest.fill(-1);
  CHECK(ring.pop(std::span<float>(rest)) == 6);
  CHECK(rest[0] == 4);
  CHECK(rest[5] == 9);
  CHECK(rest[6] == -1); // за пределами прочитанного out не меняется

  CHECK(ring.pop(std::span<float>(rest)) == 0);
}

TEST_CASE("очередь: два потока — ничего не теряется и не путается") {
  struct Mark {
    std::uint64_t index;
    double time;
  };

  SpscRing<Mark, 1024> ring;
  constexpr std::uint64_t kCount = 1'000'000;

  std::thread producer([&ring] {
    for (std::uint64_t i = 0; i < kCount;) {
      if (ring.push(Mark{.index = i, .time = static_cast<double>(i) * 0.5}))
        ++i;
      else
        std::this_thread::yield();
    }
  });

  std::uint64_t expected = 0;
  bool ordered = true;
  while (expected < kCount) {
    if (const auto mark = ring.pop()) {
      ordered = ordered && mark->index == expected &&
                mark->time == static_cast<double>(expected) * 0.5;
      ++expected;
    }
  }

  producer.join();
  CHECK(ordered);
  CHECK(expected == kCount);
}

TEST_CASE("очередь: пачки из двух потоков — поток значений непрерывен") {
  // Пишущий кладёт пачки разной длины, читающий забирает кусками другой длины:
  // так, как звуковой поток отдаёт блоки рабочему.
  auto ring = std::make_unique<SpscRing<std::uint32_t, 4096>>();
  constexpr std::uint32_t kCount = 2'000'000;

  std::thread producer([&ring] {
    std::vector<std::uint32_t> batch;
    std::uint32_t next = 0;
    std::uint32_t length = 1;
    while (next < kCount) {
      batch.clear();
      for (std::uint32_t i = 0; i < length && next + i < kCount; ++i)
        batch.push_back(next + i);

      if (ring->push(std::span<const std::uint32_t>(batch))) {
        next += static_cast<std::uint32_t>(batch.size());
        length = length % 700 + 13;
      } else {
        std::this_thread::yield();
      }
    }
  });

  std::array<std::uint32_t, 333> chunk{};
  std::uint32_t expected = 0;
  bool continuous = true;
  while (expected < kCount) {
    const std::size_t count = ring->pop(std::span<std::uint32_t>(chunk));
    for (std::size_t i = 0; i < count; ++i) {
      continuous = continuous && chunk[i] == expected;
      ++expected;
    }
  }

  producer.join();
  CHECK(continuous);
  CHECK(expected == kCount);
}
