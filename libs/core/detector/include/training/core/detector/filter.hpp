#pragma once

#include "training/core/types.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace training::core::detector {

/// Фильтр звукового потока: базовый интерфейс фильтров и детекторов, которые
/// обрабатывают поток пачками сэмплов.
///
/// Многопоточность: методы реализации можно звать из любых потоков
/// одновременно.
class Filter {
public:
  /// Позволяет удалять реализацию через указатель на `Filter`.
  virtual ~Filter() = default;

  /// Применяет фильтр к следующей пачке сэмплов потока `samples`: каждый
  /// сэмпл заменяется обработанным на том же месте.
  virtual void process(std::span<double> samples) = 0;

  /// Частота дискретизации потока, с которой работает фильтр.
  virtual SampleRate sampleRate() const = 0;

protected:
  /// Создаёт фильтр; своих данных у `Filter` нет, их хранит реализация.
  Filter() = default;

  /// Копирует часть `Filter` — данных у неё нет.
  Filter(const Filter &) = default;

  /// Перемещает часть `Filter` — данных у неё нет.
  Filter(Filter &&) = default;

  /// Присваивает часть `Filter` — данных у неё нет.
  Filter &operator=(const Filter &) = default;

  /// Присваивает часть `Filter` перемещением — данных у неё нет.
  Filter &operator=(Filter &&) = default;
};

/// Цепочка фильтров: применяет их по очереди, каждый следующий — к результату
/// предыдущего.
///
/// Многопоточность: все методы можно звать из любых потоков одновременно;
/// пачка проходит всю цепочку под замком цепочки.
class Serial final : public Filter {
public:
  /// Создаёт цепочку частоты `sampleRate` из фильтров `filters` в порядке
  /// вызова. Цепочка без фильтров оставляет сэмплы как есть.
  ///
  /// @return пусто, если среди фильтров есть пустой указатель или фильтр
  ///   работает с частотой дискретизации, отличной от `sampleRate`.
  static std::optional<Serial> create(SampleRate sampleRate,
                                      std::vector<std::unique_ptr<Filter>> filters);

  /// Забирает цепочку `other` под её замком.
  Serial(Serial &&other) noexcept;

  /// Копии нет: состояние фильтров держит замок цепочки.
  Serial(const Serial &) = delete;

  /// Присваивания копией нет: состояние фильтров держит замок цепочки.
  Serial &operator=(const Serial &) = delete;

  /// Присваивания перемещением нет: состояние фильтров держит замок цепочки.
  Serial &operator=(Serial &&) = delete;

  /// Позволяет удалять цепочку через указатель на `Filter`.
  ~Serial() override = default;

  /// Вызывает фильтры по очереди: каждый следующий получает результат
  /// предыдущего, в `samples` остаётся результат последнего. Без фильтров
  /// `samples` остаются как есть.
  void process(std::span<double> samples) override;

  /// Частота дискретизации потока — общая у всех фильтров цепочки.
  SampleRate sampleRate() const override { return sampleRate_; }

private:
  // Детектор собирает цепочку из своих фильтров одной частоты
  // (`Detector::filter`).
  friend class Detector;

  /// Цепочка из фильтров `filters` частоты `sampleRate`.
  Serial(std::vector<std::unique_ptr<Filter>> filters, SampleRate sampleRate);

  /// Забирает цепочку `other`; её замок держит вызывающий.
  Serial(Serial &&other, const std::scoped_lock<std::mutex> & /*lock*/);

  // Состояние фильтров во время прохода пачки — под этим замком.
  std::mutex mutex_;
  std::vector<std::unique_ptr<Filter>> filters_;
  SampleRate sampleRate_;
};

} // namespace training::core::detector
