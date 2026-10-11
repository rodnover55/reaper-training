#include "training/trainer/onset_search.hpp"

#include <algorithm>
#include <ranges>
#include <utility>

namespace training::trainer {

OnsetSearch::OnsetSearch(const RunState &run, const RunLeadIn &leadIn, Journal journal)
    : run_(run), leadIn_(leadIn), journal_(std::move(journal)) {}

AudioOnset OnsetSearch::onsetOf(const StreamBlock &start, double offset, double levelDb,
                                Moment run, std::uint64_t runStartSample) const {
  AudioOnset hit{.blockPosition = start.block.position,
                 .offset = offset,
                 .sampleRate = start.block.sampleRate,
                 .levelDb = levelDb,
                 .monotonic = start.block.monotonic,
                 .run = run,
                 .beforeStart = false};

  // До конца отсчёта хост держит позицию блоков на позиции начала, а до Play
  // ведёт часы остановки: место блока — по пути от него до конца отсчёта.
  const std::uint64_t countInEnd = runStartSample + leadIn_.countIn.load();
  if (start.block.firstSample < countInEnd) {
    hit.beforeStart = true;
    hit.blockPosition =
        leadIn_.position.load() - static_cast<double>(countInEnd - start.block.firstSample) /
                                      start.block.sampleRate * leadIn_.rate.load();
  }
  return hit;
}

void OnsetSearch::give(const AudioOnset &hit, std::vector<AudioOnset> &onsets) const {
  write(journal_, "onset at {} P={:.6f} i={:.3f} level={:.1f} dB", momentText(hit.monotonic),
        hit.blockPosition, hit.offset, hit.levelDb);
  onsets.push_back(hit);
}

void OnsetSearch::process(const AudioBlock &block, std::span<const float> samples,
                          double silenceDb, std::vector<AudioOnset> &onsets) {
  // Окна детектора посчитаны в сэмплах его частоты: звук другой частоты
  // разбирает новый детектор, и прошлый звук к нему не относится.
  if (!detector_ || block.sampleRate != detectorRate_) {
    detector_.emplace(onset::Settings{.sampleRate = block.sampleRate, .silenceDb = silenceDb});
    detectorRate_ = block.sampleRate;
    appliedSilenceDb_ = silenceDb;
    history_.clear();
    early_.clear();
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

  // Новый запуск поток не прерывает: меняется только первый блок запуска.
  // Блок, взятый раньше начала, — до Play, даже если подан после начала.
  const Moment run = run_.load();
  if (run != seenRun_) {
    seenRun_ = run;
    runStartSample_.reset();
  }
  history_.push_back({.streamStart = detector_->position(), .block = block});

  // Первый блок запуска: атаки до Play, найденные без запуска, — перед ним.
  if (run != kNoRun && block.monotonic >= run && !runStartSample_) {
    runStartSample_ = block.firstSample;
    for (const EarlyOnset &early : early_)
      give(onsetOf(early.start, early.offset, early.levelDb, run, block.firstSample), onsets);
    early_.clear();
  }

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
    const double offset = found.position - static_cast<double>(start->streamStart);

    // Без запуска атака ждёт его как нота до Play. Атака из блока прошлого
    // запуска — тоже: этот запуск кончился.
    if (!runStartSample_ || run == kNoRun) {
      early_.push_back({.start = *start, .offset = offset, .levelDb = found.levelDb});
      continue;
    }
    give(onsetOf(*start, offset, found.levelDb, run, *runStartSample_), onsets);
  }

  // Блоки старше kOnsetHistorySeconds звука больше не понадобятся, и атаки
  // из них до Play уже не ждут.
  const auto keep = static_cast<std::uint64_t>(kOnsetHistorySeconds * block.sampleRate);
  const std::uint64_t now = detector_->position();
  const auto fresh = [now, keep](std::uint64_t streamStart) {
    return streamStart + keep >= now;
  };
  const auto stale = std::ranges::find_if(history_, [&fresh](const StreamBlock &candidate) {
    return fresh(candidate.streamStart);
  });
  history_.erase(history_.begin(), stale);
  std::erase_if(early_,
                [&fresh](const EarlyOnset &early) { return !fresh(early.start.streamStart); });
}

} // namespace training::trainer
