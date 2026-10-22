#pragma once

#include "training/core/clock.hpp"
#include "training/core/detector/filter.hpp"
#include "training/core/types.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <numbers>
#include <optional>
#include <span>
#include <utility>

namespace training::core::detector {

/// ВЧ-фильтр второго порядка по Audio EQ Cookbook (RBJ): пропускает звук выше
/// частоты среза и ослабляет звук ниже неё на 12 дБ на октаву. Через него
/// поиск атаки и калибровка смотрят на верх спектра, где слышен щелчок
/// медиатора.
///
/// Многопоточность: все методы можно звать из любых потоков одновременно.
class HighPass final : public Filter {
public:
  /// Добротность по умолчанию: 1/√2, фильтр Баттерворта.
  static constexpr double kButterworthQ = 1.0 / std::numbers::sqrt2;

  /// Создаёт фильтр, готовый к началу потока: прошлые сэмплы — нули.
  ///
  /// @param cutoffHz частота среза, Гц.
  /// @param sampleRate частота дискретизации потока.
  /// @param q добротность: форма перехода около частоты среза.
  /// @return пусто, если `cutoffHz` вне промежутка от нуля до половины
  ///   `sampleRate` (обе границы — вне) или `q` — ноль или меньше.
  static std::optional<HighPass> create(double cutoffHz, core::SampleRate sampleRate,
                                        double q = kButterworthQ) {
    // Условие — о том, что годится, а не о том, что нет: NaN в `cutoffHz` или
    // `q` не проходит ни одно сравнение и так тоже отказывает.
    const bool valid = cutoffHz > 0.0 && cutoffHz < sampleRate.hz() / 2.0 && q > 0.0;
    if (!valid)
      return std::nullopt;
    return HighPass(cutoffHz, sampleRate, q);
  }

  /// Копия фильтра `other`, снятая под его замком: те же коэффициенты и
  /// прошлые сэмплы. Замок у копии свой.
  HighPass(const HighPass &other) : HighPass(other, std::scoped_lock(other.mutex_)) {}

  /// Забирает фильтр `other` под его замком.
  HighPass(HighPass &&other) noexcept : HighPass(other, std::scoped_lock(other.mutex_)) {}

  /// Ставит коэффициенты и прошлые сэмплы фильтра `other`, снятые под замками
  /// обоих.
  HighPass &operator=(const HighPass &other) {
    if (this == &other)
      return *this;
    const std::scoped_lock lock(mutex_, other.mutex_);
    Filter::operator=(other);
    sampleRate_ = other.sampleRate_;
    b0_ = other.b0_;
    b1_ = other.b1_;
    b2_ = other.b2_;
    a1_ = other.a1_;
    a2_ = other.a2_;
    x1_ = other.x1_;
    x2_ = other.x2_;
    y1_ = other.y1_;
    y2_ = other.y2_;
    return *this;
  }

  /// Забирает фильтр `other` под замками обоих.
  HighPass &operator=(HighPass &&other) noexcept { return *this = other; }

  /// Позволяет удалять фильтр через указатель на `Filter`.
  ~HighPass() override = default;

  /// Забывает прошлые сэмплы: следующий сэмпл фильтруется как первый в потоке.
  void reset() {
    const std::scoped_lock lock(mutex_);
    x1_ = x2_ = y1_ = y2_ = 0.0;
  }

  /// Фильтрует следующий кусок потока `samples`: каждый сэмпл заменяется
  /// сэмплом после фильтра на том же месте.
  void process(std::span<double> samples) override {
    const std::scoped_lock lock(mutex_);
    for (double &sample : samples) {
      const double filtered = b0_ * sample + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
      x2_ = x1_;
      x1_ = sample;
      y2_ = y1_;
      y1_ = filtered;
      sample = filtered;
    }
  }

  /// Частота дискретизации потока.
  core::SampleRate sampleRate() const override { return sampleRate_; }

private:
  /// Фильтр с частотой среза `cutoffHz` и добротностью `q`: `cutoffHz` больше
  /// нуля и меньше половины `sampleRate`, `q` больше нуля.
  HighPass(double cutoffHz, core::SampleRate sampleRate, double q) : sampleRate_(sampleRate) {
    // Формулы RBJ: w0 = 2π·f0/Fs, alpha = sin(w0) / 2Q.
    const double w0 = 2.0 * std::numbers::pi * cutoffHz / sampleRate.hz();
    const double cosW0 = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;

    b0_ = (1.0 + cosW0) / 2.0 / a0;
    b1_ = -(1.0 + cosW0) / a0;
    b2_ = b0_;
    a1_ = -2.0 * cosW0 / a0;
    a2_ = (1.0 - alpha) / a0;
  }

  /// Копия фильтра `other`; его замок держит вызывающий.
  HighPass(const HighPass &other, const std::scoped_lock<std::mutex> & /*lock*/)
      : Filter(other), sampleRate_(other.sampleRate_), b0_(other.b0_), b1_(other.b1_),
        b2_(other.b2_), a1_(other.a1_), a2_(other.a2_), x1_(other.x1_), x2_(other.x2_),
        y1_(other.y1_), y2_(other.y2_) {}

  // Прошлые сэмплы — под этим замком.
  mutable std::mutex mutex_;

  core::SampleRate sampleRate_;

  // Коэффициенты, нормированные на a0.
  double b0_ = 0.0;
  double b1_ = 0.0;
  double b2_ = 0.0;
  double a1_ = 0.0;
  double a2_ = 0.0;

  // Прошлые два входа и выхода.
  double x1_ = 0.0;
  double x2_ = 0.0;
  double y1_ = 0.0;
  double y2_ = 0.0;
};

/// Огибающая звука — пиковый детектор: поднимается сразу до модуля сэмпла, а
/// когда звук стихает, спадает в e раз за время спада. С ней поиск атаки
/// сравнивает порог тишины.
///
/// Многопоточность: все методы можно звать из любых потоков одновременно.
class Envelope final : public Filter {
public:
  /// Время спада по умолчанию.
  static constexpr core::Interval kDefaultRelease = 10 * core::Interval::kMillisecond;

  /// Создаёт огибающую на нуле — как у тишины до начала потока.
  ///
  /// @param sampleRate частота дискретизации потока.
  /// @param release за сколько огибающая спадает в e раз.
  explicit Envelope(core::SampleRate sampleRate, core::Interval release = kDefaultRelease)
      : sampleRate_(sampleRate),
        release_(std::exp(-1.0 / (release / core::Interval::kSecond * sampleRate.hz()))) {}

  /// Копия огибающей `other`, снятая под её замком: та же частота, время спада
  /// и значение огибающей. Замок у копии свой.
  Envelope(const Envelope &other) : Envelope(other, std::scoped_lock(other.mutex_)) {}

  /// Забирает огибающую `other` под её замком.
  Envelope(Envelope &&other) noexcept : Envelope(other, std::scoped_lock(other.mutex_)) {}

  /// Ставит частоту, время спада и значение огибающей `other`, снятые под
  /// замками обеих.
  Envelope &operator=(const Envelope &other) {
    if (this == &other)
      return *this;
    const std::scoped_lock lock(mutex_, other.mutex_);
    Filter::operator=(other);
    sampleRate_ = other.sampleRate_;
    release_ = other.release_;
    value_ = other.value_;
    return *this;
  }

  /// Забирает огибающую `other` под замками обеих.
  Envelope &operator=(Envelope &&other) noexcept { return *this = other; }

  /// Позволяет удалять огибающую через указатель на `Filter`.
  ~Envelope() override = default;

  /// Возвращает огибающую на ноль: вычисление начинается заново.
  void reset() {
    const std::scoped_lock lock(mutex_);
    value_ = 0.0;
  }

  /// Ведёт огибающую по следующему куску потока `samples`: каждый сэмпл
  /// заменяется огибающей на нём, доли полной шкалы, на том же месте.
  void process(std::span<double> samples) override {
    const std::scoped_lock lock(mutex_);
    for (double &sample : samples) {
      value_ = std::max(std::abs(sample), value_ * release_);
      sample = value_;
    }
  }

  /// Частота дискретизации потока.
  core::SampleRate sampleRate() const override { return sampleRate_; }

private:
  /// Копия огибающей `other`; её замок держит вызывающий.
  Envelope(const Envelope &other, const std::scoped_lock<std::mutex> & /*lock*/)
      : Filter(other), sampleRate_(other.sampleRate_), release_(other.release_),
        value_(other.value_) {}

  // Значение огибающей — под этим замком.
  mutable std::mutex mutex_;
  core::SampleRate sampleRate_;
  double release_ = 0.0;
  double value_ = 0.0;
};

} // namespace training::core::detector
