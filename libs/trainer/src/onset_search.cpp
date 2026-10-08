#include "training/trainer/onset_search.hpp"

#include <algorithm>
#include <ranges>
#include <utility>

namespace training::trainer {

OnsetSearch::OnsetSearch(const RunState &run, Journal journal)
    : run_(run), journal_(std::move(journal)) {}

void OnsetSearch::forget() {
  detector_.reset();
  history_.clear();
  previousPosition_.reset();
  expectedSample_.reset();
}

void OnsetSearch::process(const AudioBlock &block, std::span<const float> samples,
                          double silenceDb, std::vector<AudioOnset> &onsets) {
  // Прошлый звук собран для другого запуска или без запуска: к этому он не
  // относится.
  const Moment runStart = run_.load();
  if (runStart != collectedFor_) {
    forget();
    collectedFor_ = runStart;
  }

  // Звук, взятый до начала запуска, — прошлого запуска или остановленного
  // транспорта, даже если звук шёл без перерыва: в поток звука запуска он не
  // входит.
  if (runStart == kNoRun || block.monotonic < runStart)
    return;

  // Окна детектора посчитаны в сэмплах его частоты: звук другой частоты
  // разбирает новый детектор, и позиции истории блоков к нему не относятся.
  if (!detector_ || block.sampleRate != detectorRate_) {
    detector_.emplace(onset::Settings{.sampleRate = block.sampleRate, .silenceDb = silenceDb});
    detectorRate_ = block.sampleRate;
    appliedSilenceDb_ = silenceDb;
    history_.clear();
    expectedSample_.reset();
  }

  if (silenceDb != appliedSilenceDb_) {
    detector_->setSilenceDb(silenceDb);
    appliedSilenceDb_ = silenceDb;
  }

  // Пропавший звук занимает время: детектор отсчитывает его без звука.
  if (expectedSample_ && block.firstSample > *expectedSample_)
    detector_->skip(block.firstSample - *expectedSample_);
  expectedSample_ = block.firstSample + samples.size();

  const bool moving = previousPosition_ && block.position != *previousPosition_;
  previousPosition_ = block.position;
  history_.push_back({.streamStart = detector_->position(), .block = block, .moving = moving});

  found_.clear();
  detector_->process(samples, found_);

  for (const onset::Onset &found : found_) {
    // Блок начала атаки — последний из начавшихся не позже неё.
    auto newestFirst = std::views::reverse(history_);
    const auto start =
        std::ranges::find_if(newestFirst, [&found](const StreamBlock &candidate) {
          return static_cast<double>(candidate.streamStart) <= found.position;
        });
    if (start == newestFirst.end())
      continue;
    if (!start->moving) {
      write(journal_, "onset dropped: position {:.6f} is not moving", start->block.position);
      continue;
    }

    const AudioOnset hit{.blockPosition = start->block.position,
                         .offset = found.position - static_cast<double>(start->streamStart),
                         .sampleRate = start->block.sampleRate,
                         .levelDb = found.levelDb,
                         .monotonic = start->block.monotonic};
    write(journal_, "onset at {} P={:.6f} i={:.3f} level={:.1f} dB", momentText(hit.monotonic),
          hit.blockPosition, hit.offset, hit.levelDb);
    onsets.push_back(hit);
  }

  // Блоки старше kOnsetHistorySeconds звука больше не понадобятся.
  const auto keep = static_cast<std::uint64_t>(kOnsetHistorySeconds * block.sampleRate);
  const std::uint64_t now = detector_->position();
  const auto stale = std::ranges::find_if(history_, [now, keep](const StreamBlock &candidate) {
    return candidate.streamStart + keep >= now;
  });
  history_.erase(history_.begin(), stale);
}

} // namespace training::trainer
