#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace training::onset {

/// ВЧ-фильтр второго порядка по Баттерворту: пропускает звук выше частоты
/// среза и ослабляет звук ниже неё на 12 дБ на октаву. Через него поиск атаки
/// и калибровка смотрят на верх спектра, где слышен щелчок медиатора.
///
/// Многопоточность: один экземпляр — один поток вызовов.
class HighPass {
public:
  /// Создаёт фильтр, готовый к началу потока: прошлые сэмплы — нули.
  ///
  /// @param cutoffHz частота среза, Гц; больше нуля и меньше половины
  ///   `sampleRate`.
  /// @param sampleRate частота дискретизации потока, Гц; больше нуля.
  HighPass(double cutoffHz, double sampleRate) {
    // Формулы RBJ: alpha = sin(w0) / 2Q, а при Q = 1/√2 знаменатель 2Q равен √2.
    const double w0 = 2.0 * std::numbers::pi * cutoffHz / sampleRate;
    const double cosW0 = std::cos(w0);
    const double alpha = std::sin(w0) / std::numbers::sqrt2;
    const double a0 = 1.0 + alpha;

    b0_ = (1.0 + cosW0) / 2.0 / a0;
    b1_ = -(1.0 + cosW0) / a0;
    b2_ = b0_;
    a1_ = -2.0 * cosW0 / a0;
    a2_ = (1.0 - alpha) / a0;
  }

  /// Забывает прошлые сэмплы: следующий сэмпл фильтруется как первый в потоке.
  void reset() { x1_ = x2_ = y1_ = y2_ = 0.0; }

  /// Фильтрует следующий сэмпл потока.
  ///
  /// @return сэмпл после фильтра.
  double process(double sample) {
    const double filtered = b0_ * sample + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
    x2_ = x1_;
    x1_ = sample;
    y2_ = y1_;
    y1_ = filtered;
    return filtered;
  }

private:
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

/// Огибающая звука: поднимается сразу до модуля сэмпла, а когда звук стихает,
/// спадает в e раз за 10 мс. С ней поиск атаки сравнивает порог тишины.
///
/// Многопоточность: один экземпляр — один поток вызовов.
class Envelope {
public:
  /// За сколько миллисекунд огибающая спадает в e раз.
  static constexpr double kReleaseMs = 10.0;

  /// Создаёт огибающую на нуле — как у тишины до начала потока.
  ///
  /// @param sampleRate частота дискретизации потока, Гц; больше нуля.
  explicit Envelope(double sampleRate)
      : release_(std::exp(-1.0 / (kReleaseMs * sampleRate / 1000.0))) {}

  /// Возвращает огибающую на ноль.
  void reset() { value_ = 0.0; }

  /// Ведёт огибающую на следующий сэмпл.
  ///
  /// @return огибающая на этом сэмпле, доли полной шкалы.
  double process(double sample) {
    value_ = std::max(std::abs(sample), value_ * release_);
    return value_;
  }

  /// Огибающая на последнем поданном сэмпле; ноль до первого и после `reset`.
  double value() const { return value_; }

private:
  double release_ = 0.0;
  double value_ = 0.0;
};

} // namespace training::onset
