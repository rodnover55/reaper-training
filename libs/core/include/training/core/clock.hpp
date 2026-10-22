#pragma once

// Время ядра тренажёра: время и интервалы между двумя значениями времени.

#include <chrono>
#include <concepts>
#include <cstdint>

namespace training::core {

/// Интервал — разница двух значений времени ядра (`Moment`), целые
/// наносекунды; бывает отрицательным. Задаётся через единицы: `5.5 * kSecond`,
/// `5 * kSecond + 500 * kMillisecond`; в числах — делением на единицу:
/// `interval / kSecond` — секунды дробным числом,
/// `static_cast<std::int64_t>(interval / kNanosecond)` — целые наносекунды.
///
/// Созданный объект: нулевой интервал.
class Interval {
public:
  /// Отношение двух интервалов — результат деления интервала на интервал;
  /// число получается преобразованием: дробное — неявно, целое — явно.
  class Ratio {
  public:
    /// Отношение дробным числом: 1 с / 250 мс — 4, 1 мс / 1 с — 0.001.
    constexpr operator double() const {
      return static_cast<double>(dividend_) / static_cast<double>(divisor_);
    }

    /// Целая часть отношения, остаток отбрасывается к нулю: 1100 мс / 250 мс
    /// — 4, −1100 мс / 250 мс — −4.
    explicit constexpr operator std::int64_t() const { return dividend_ / divisor_; }

  private:
    friend class Interval;

    /// Отношение `dividend` наносекунд к `divisor` наносекундам.
    constexpr Ratio(std::int64_t dividend, std::int64_t divisor)
        : dividend_(dividend), divisor_(divisor) {}

    std::int64_t dividend_;
    std::int64_t divisor_;
  };

  /// Наносекунда.
  static const Interval kNanosecond;

  /// Микросекунда.
  static const Interval kMicrosecond;

  /// Миллисекунда.
  static const Interval kMillisecond;

  /// Секунда.
  static const Interval kSecond;

  /// Создаёт нулевой интервал.
  constexpr Interval() = default;

  /// Длина интервала без знака.
  constexpr Interval abs() const { return *this < Interval() ? -*this : *this; }

  /// Интервал той же длины с другим знаком.
  constexpr Interval operator-() const { return Interval(-value_); }

  /// Прибавляет интервал `other`.
  constexpr Interval &operator+=(Interval other) {
    value_ += other.value_;
    return *this;
  }

  /// Вычитает интервал `other`.
  constexpr Interval &operator-=(Interval other) {
    value_ -= other.value_;
    return *this;
  }

  /// Сумма интервалов.
  friend constexpr Interval operator+(Interval left, Interval right) { return left += right; }

  /// Разность интервалов.
  friend constexpr Interval operator-(Interval left, Interval right) { return left -= right; }

  /// Интервал `interval`, умноженный на целое `factor`.
  template <std::integral Factor>
  friend constexpr Interval operator*(Interval interval, Factor factor) {
    return Interval(interval.value_ * static_cast<std::int64_t>(factor));
  }

  /// Интервал `interval`, умноженный на целое `factor`.
  template <std::integral Factor>
  friend constexpr Interval operator*(Factor factor, Interval interval) {
    return interval * factor;
  }

  /// Интервал `interval`, умноженный на `factor`, округлённый до наносекунды.
  friend constexpr Interval operator*(Interval interval, double factor) {
    return Interval(std::chrono::nanoseconds(
        rounded(static_cast<double>(interval.value_.count()) * factor)));
  }

  /// Интервал `interval`, умноженный на `factor`, округлённый до наносекунды.
  friend constexpr Interval operator*(double factor, Interval interval) {
    return interval * factor;
  }

  /// Интервал `interval`, делённый на `divisor`, округлённый до наносекунды.
  ///
  /// @param divisor отличен от нуля.
  friend constexpr Interval operator/(Interval interval, double divisor) {
    return Interval(std::chrono::nanoseconds(
        rounded(static_cast<double>(interval.value_.count()) / divisor)));
  }

  /// Во сколько раз интервал длиннее интервала `unit` (`Ratio`): 1 с / 250 мс
  /// — 4, `interval / kSecond` — интервал в секундах.
  ///
  /// @param unit отличен от нуля.
  constexpr Ratio operator/(Interval unit) const {
    return Ratio(value_.count(), unit.value_.count());
  }

  /// Сравнивает интервалы по длине со знаком.
  friend constexpr auto operator<=>(Interval, Interval) = default;

  /// Правда, если у интервалов одна длина и знак.
  friend constexpr bool operator==(Interval, Interval) = default;

private:
  /// Интервал длиной `value`.
  explicit constexpr Interval(std::chrono::nanoseconds value) : value_(value) {}

  /// Ближайшее к `value` целое; середина — от нуля.
  static constexpr std::int64_t rounded(double value) {
    return static_cast<std::int64_t>(value < 0.0 ? value - 0.5 : value + 0.5);
  }

  std::chrono::nanoseconds value_{0};
};

inline constexpr Interval Interval::kNanosecond{std::chrono::nanoseconds(1)};
inline constexpr Interval Interval::kMicrosecond{std::chrono::microseconds(1)};
inline constexpr Interval Interval::kMillisecond{std::chrono::milliseconds(1)};
inline constexpr Interval Interval::kSecond{std::chrono::seconds(1)};

/// Время ядра: интервал от точки отсчёта — одной на всё время, которое
/// сравнивается.
///
/// Созданный объект: точка отсчёта.
class Moment {
public:
  /// Создаёт точку отсчёта.
  constexpr Moment() = default;

  /// Сдвигает время на интервал `interval` вперёд.
  constexpr Moment &operator+=(Interval interval) {
    sinceOrigin_ += interval;
    return *this;
  }

  /// Сдвигает время на интервал `interval` назад.
  constexpr Moment &operator-=(Interval interval) {
    sinceOrigin_ -= interval;
    return *this;
  }

  /// Время `time`, сдвинутое на интервал `interval` вперёд.
  friend constexpr Moment operator+(Moment time, Interval interval) {
    return time += interval;
  }

  /// Время `time`, сдвинутое на интервал `interval` вперёд.
  friend constexpr Moment operator+(Interval interval, Moment time) {
    return time += interval;
  }

  /// Время `time`, сдвинутое на интервал `interval` назад.
  friend constexpr Moment operator-(Moment time, Interval interval) {
    return time -= interval;
  }

  /// Интервал от времени `earlier` до времени `later`.
  friend constexpr Interval operator-(Moment later, Moment earlier) {
    return later.sinceOrigin_ - earlier.sinceOrigin_;
  }

  /// Сравнивает время: раньше — меньше.
  friend constexpr auto operator<=>(Moment, Moment) = default;

  /// Правда, если время одно и то же.
  friend constexpr bool operator==(Moment, Moment) = default;

private:
  Interval sinceOrigin_;
};

} // namespace training::core
