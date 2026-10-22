#include "training/core/detector/filter.hpp"

#include <algorithm>
#include <utility>

namespace training::core::detector {

std::optional<Serial> Serial::create(SampleRate sampleRate,
                                     std::vector<std::unique_ptr<Filter>> filters) {
  if (!std::ranges::all_of(filters, [sampleRate](const std::unique_ptr<Filter> &filter) {
        return filter && filter->sampleRate() == sampleRate;
      }))
    return std::nullopt;
  return Serial(std::move(filters), sampleRate);
}

Serial::Serial(std::vector<std::unique_ptr<Filter>> filters, SampleRate sampleRate)
    : filters_(std::move(filters)), sampleRate_(sampleRate) {}

Serial::Serial(Serial &&other) noexcept
    : Serial(std::move(other), std::scoped_lock(other.mutex_)) {}

Serial::Serial(Serial &&other, const std::scoped_lock<std::mutex> & /*lock*/)
    : filters_(std::move(other.filters_)), sampleRate_(other.sampleRate_) {}

void Serial::process(std::span<double> samples) {
  const std::scoped_lock lock(mutex_);
  for (const std::unique_ptr<Filter> &filter : filters_)
    filter->process(samples);
}

} // namespace training::core::detector
