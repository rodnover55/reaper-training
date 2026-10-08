#include <doctest/doctest.h>

#include "synth.hpp"
#include "wav.hpp"

#include "training/trainer/calibration.hpp"
#include "training/trainer/measurement.hpp"
#include "training/trainer/transport.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using training::onset::CalibrationStep;
using training::onset::Verdict;
using training::test::evenPlucks;
using training::test::Pluck;
using training::test::readWav;
using training::test::render;
using training::test::SynthSettings;
using training::trainer::CalibrationPhase;
using training::trainer::CalibrationUpdate;
using training::trainer::Calibrator;
using training::trainer::Measurement;
using training::trainer::Measuring;
using training::trainer::Moment;
using training::trainer::phaseOf;

using namespace std::chrono_literals;

namespace {

constexpr double kSampleRate = 48000.0;

std::size_t samplesOf(double seconds, double sampleRate = kSampleRate) {
  return static_cast<std::size_t>(std::llround(seconds * sampleRate));
}

/// Белый шум с пиком `db` dBFS длиной `seconds` с.
std::vector<float> noise(double seconds, double db, std::uint32_t seed = 1) {
  return render({}, samplesOf(seconds), SynthSettings{.noiseDb = db, .seed = seed});
}

/// Цифровая тишина длиной `seconds` с.
std::vector<float> silence(double seconds) { return std::vector<float>(samplesOf(seconds)); }

std::vector<float> joined(std::initializer_list<std::vector<float>> parts) {
  std::vector<float> all;
  for (const std::vector<float> &part : parts)
    all.insert(all.end(), part.begin(), part.end());
  return all;
}

/// Глушёные ноты DI-гитары из `tests/data` между тихими шумами: перед ними —
/// шаг тишины, после — 2 с, за которые калибровка подводит итог.
std::vector<float> mutedQuarters() {
  const auto recording = readWav(std::filesystem::path(TRAINING_TEST_DATA_DIR) / "onset" /
                                 "di_muted_quarters.wav");
  REQUIRE(recording.sampleRate == kSampleRate);
  return joined({noise(3.3, -96.0, 2), recording.samples, noise(2.0, -96.0, 3)});
}

/// Восемь щипков с перегрузом через 0.5 с после шага тишины и 2 с после них.
std::vector<float> clippingPlucks() {
  const std::size_t first = samplesOf(3.6);
  std::vector<Pluck> plucks = evenPlucks(first, kSampleRate / 2.0, 8, {110.0, 147.0}, 1.0);
  plucks.push_back(Pluck{.start = plucks.back().start + samplesOf(0.4), .amplitude = 0.0});
  return render(plucks, first + samplesOf(4.0 + 2.0),
                SynthSettings{.noiseDb = -100.0, .drive = 6.0});
}

/// Удар по струнам, заглушённый через 50 мс, и 0.5 с после него на тихом шуме.
std::vector<float> strike() {
  return render({Pluck{.start = samplesOf(0.1), .amplitude = 0.3},
                 Pluck{.start = samplesOf(0.15), .amplitude = 0.0}},
                samplesOf(0.6), SynthSettings{.noiseDb = -96.0, .seed = 5});
}

/// Состояние измерения: тренажёр ждёт запуска на канале «Input 1».
Measurement waiting() {
  return Measurement{.measuring = Measuring::Waiting,
                     .inputName = "Input 1",
                     .midi = false,
                     .compensationMs = 0.0,
                     .rate = 1.0,
                     .calibrationCancelled = false};
}

/// Калибровка с журналом и часами: звук подаётся кусками, как его отдаёт
/// звуковая карта, — по куску на тик, время идёт вместе со звуком.
struct Bench {
  std::vector<std::string> lines;
  Calibrator calibrator{[this](std::string_view line) { lines.emplace_back(line); }};
  Moment now{100s};
  /// Пороги, которые калибровка велела поставить.
  std::vector<double> set;

  Bench() = default;
  Bench(const Bench &) = delete;
  Bench &operator=(const Bench &) = delete;
  Bench(Bench &&) = delete;
  Bench &operator=(Bench &&) = delete;
  ~Bench() = default;

  bool start() { return calibrator.start(false, waiting(), 0, -50.0, now); }

  CalibrationUpdate tick() {
    const CalibrationUpdate update = calibrator.update(now);
    if (update.silenceDb)
      set.push_back(*update.silenceDb);
    return update;
  }

  /// Подаёт `signal` кусками по 512 сэмплов частоты `sampleRate`.
  void play(std::span<const float> signal, double sampleRate = kSampleRate) {
    for (std::size_t from = 0; from < signal.size(); from += 512) {
      const std::size_t length = std::min<std::size_t>(512, signal.size() - from);
      calibrator.feed(signal.subspan(from, length), sampleRate, false);
      now += std::chrono::round<std::chrono::nanoseconds>(
          std::chrono::duration<double>(static_cast<double>(length) / sampleRate));
      tick();
    }
  }

  bool logged(std::string_view line) const {
    return std::ranges::find(lines, line) != lines.end();
  }
};

} // namespace

TEST_CASE("Калибровка тренажёра: запуск только на канале звуковой карты при стоящем "
          "транспорте") {
  Bench bench;
  CHECK_FALSE(bench.calibrator.start(true, waiting(), 0, -50.0, bench.now));

  Measurement midi = waiting();
  midi.midi = true;
  CHECK_FALSE(bench.calibrator.start(false, midi, 0, -50.0, bench.now));

  Measurement absent = waiting();
  absent.measuring = Measuring::NoInput;
  absent.inputName.clear();
  CHECK_FALSE(bench.calibrator.start(false, absent, 0, -50.0, bench.now));
  CHECK_FALSE(bench.calibrator.session());
  CHECK(bench.lines.empty());

  REQUIRE(bench.calibrator.start(false, waiting(), 1, -42.0, bench.now));
  REQUIRE(bench.calibrator.session());
  CHECK(bench.calibrator.session()->channelName == "Input 1");
  CHECK(bench.calibrator.session()->previousSilenceDb == -42.0);
  CHECK_FALSE(bench.calibrator.session()->calibration);
  CHECK(bench.calibrator.listening());
  CHECK_FALSE(bench.calibrator.finished());
  CHECK(bench.lines == std::vector<std::string>{
                           "calibration: started on input 2 (Input 1), silence -42 dBFS"});

  // Отказ прежнюю сессию не трогает, новый запуск её кончает.
  CHECK_FALSE(bench.calibrator.start(true, waiting(), 0, -50.0, bench.now));
  CHECK(bench.calibrator.session());
  REQUIRE(bench.start());
  CHECK(bench.lines.size() == 3);
  CHECK(bench.lines[1] == "calibration: ended");
}

TEST_CASE("Калибровка тренажёра: глушёные ноты DI-гитары ставят порог") {
  Bench bench;
  REQUIRE(bench.start());
  bench.play(mutedQuarters());

  const auto &session = bench.calibrator.session();
  REQUIRE(session);
  REQUIRE(session->calibration);
  REQUIRE(session->calibration->result());
  const auto &result = *session->calibration->result();
  CHECK(result.verdict == Verdict::Good);
  CHECK(session->finished);
  CHECK(bench.calibrator.finished());
  CHECK(bench.calibrator.listening());

  // Порог велено поставить один раз — тот, что посчитала калибровка.
  CHECK(bench.set == std::vector<double>{result.thresholdDb});
  CHECK(bench.logged("calibration: 48000 Hz, silence step"));
  CHECK(bench.logged("calibration: note 8 attack " +
                     fmt::format("{:.1f}", session->calibration->attacksDb()[7]) + " dBFS"));
  CHECK(bench.logged(fmt::format("calibration: silence -50 -> {} dBFS", result.thresholdDb)));
}

TEST_CASE("Калибровка тренажёра: смена частоты начинает заново, после итога — нет") {
  Bench bench;
  REQUIRE(bench.start());
  bench.play(noise(1.0, -96.0));
  bench.play(noise(0.5, -96.0), 44100.0);

  const auto &session = bench.calibrator.session();
  REQUIRE(session);
  REQUIRE(session->calibration);
  CHECK(session->calibration->sampleRate() == 44100.0);
  CHECK(session->calibration->step() == CalibrationStep::Silence);
  CHECK(bench.logged("calibration: 44100 Hz, silence step"));

  Bench done;
  REQUIRE(done.start());
  done.play(mutedQuarters());
  REQUIRE(done.calibrator.finished());
  const std::size_t lines = done.lines.size();
  done.play(noise(0.5, -96.0), 44100.0);
  CHECK(done.calibrator.session()->calibration->sampleRate() == kSampleRate);
  CHECK(done.calibrator.session()->calibration->result());
  CHECK(done.lines.size() == lines);
}

TEST_CASE("Калибровка тренажёра: разрыв в звуке начинает шаг тишины заново") {
  Bench bench;
  REQUIRE(bench.start());
  bench.play(noise(2.0, -96.0));
  const auto &calibration = bench.calibrator.session()->calibration;
  REQUIRE(calibration);
  REQUIRE(calibration->silenceLeft() < 1.5);

  const std::vector<float> more = noise(0.1, -96.0, 4);
  bench.calibrator.feed(std::span(more).first(512), kSampleRate, true);
  bench.tick();
  CHECK(calibration->silenceLeft() > 2.9);
  CHECK(bench.logged("calibration: gap in the input"));
}

TEST_CASE("Калибровка тренажёра: тишина — итог без нот, порог не меняется") {
  Bench bench;
  REQUIRE(bench.start());
  bench.play(silence(3.1 + 15.5));

  REQUIRE(bench.calibrator.finished());
  const auto &result = bench.calibrator.session()->calibration->result();
  REQUIRE(result);
  CHECK(result->verdict == Verdict::NotEnoughNotes);
  CHECK(result->notes == 0);
  CHECK(bench.set.empty());
  // «Apply anyway» без нот ничего не ставит, и сессия остаётся.
  CHECK_FALSE(bench.calibrator.applyAnyway());
  CHECK(bench.calibrator.session());
}

TEST_CASE("Калибровка тренажёра: перегруз — порог только по «Apply anyway»") {
  Bench bench;
  REQUIRE(bench.start());
  bench.play(clippingPlucks());

  REQUIRE(bench.calibrator.finished());
  const auto result = *bench.calibrator.session()->calibration->result();
  CHECK(result.verdict == Verdict::Clipping);
  CHECK(bench.set.empty());

  bench.lines.clear();
  CHECK(bench.calibrator.applyAnyway() == std::clamp(result.thresholdDb, -90.0, -10.0));
  CHECK_FALSE(bench.calibrator.session());
  REQUIRE(bench.lines.size() == 2);
  CHECK(bench.lines[0] == fmt::format("calibration: applied anyway, silence {} dBFS",
                                      std::clamp(result.thresholdDb, -90.0, -10.0)));
  CHECK(bench.lines[1] == "calibration: ended");
}

TEST_CASE("Калибровка тренажёра: без звука 2 с — обрыв без итога") {
  Bench bench;
  REQUIRE(bench.start());
  bench.now += 1900ms;
  CHECK_FALSE(bench.tick().changed);
  CHECK_FALSE(bench.calibrator.session()->noSound);

  bench.now += 100ms;
  CHECK(bench.tick().changed);
  REQUIRE(bench.calibrator.session());
  CHECK(bench.calibrator.session()->noSound);
  CHECK(bench.calibrator.finished());
  CHECK_FALSE(bench.calibrator.listening());
  CHECK(bench.logged("calibration: no sound from the audio device for 2 s"));

  // Звук после обрыва в калибровку не идёт.
  bench.play(noise(0.5, -96.0));
  CHECK_FALSE(bench.calibrator.session()->calibration);
  CHECK_FALSE(bench.tick().changed);
}

TEST_CASE("Калибровка тренажёра: звук пропал посреди шага — обрыв через 2 с") {
  Bench bench;
  REQUIRE(bench.start());
  bench.play(noise(1.0, -96.0));
  bench.now += 1990ms;
  bench.tick();
  CHECK_FALSE(bench.calibrator.session()->noSound);
  bench.now += 10ms;
  bench.tick();
  CHECK(bench.calibrator.session()->noSound);
  CHECK(bench.calibrator.session()->calibration);
}

TEST_CASE("Калибровка тренажёра: запуск транспорта прерывает шаги и закрывает итог") {
  Bench steps;
  REQUIRE(steps.start());
  steps.play(noise(1.0, -96.0));
  steps.calibrator.onStart();
  CHECK_FALSE(steps.calibrator.session());
  CHECK(steps.calibrator.cancelledByPlayback());
  CHECK(steps.logged("calibration: cancelled by playback"));
  CHECK(steps.lines.back() == "calibration: ended");
  steps.calibrator.onStop();
  CHECK_FALSE(steps.calibrator.cancelledByPlayback());

  Bench result;
  REQUIRE(result.start());
  result.play(mutedQuarters());
  REQUIRE(result.calibrator.finished());
  result.calibrator.onStart();
  CHECK_FALSE(result.calibrator.session());
  CHECK_FALSE(result.calibrator.cancelledByPlayback());
  CHECK(result.logged("calibration: closed by playback"));
}

TEST_CASE("Калибровка тренажёра: новая калибровка снимает отметку прерывания") {
  Bench bench;
  REQUIRE(bench.start());
  bench.calibrator.onStart();
  bench.calibrator.onStop();
  REQUIRE(bench.start());
  bench.calibrator.onStart();
  REQUIRE(bench.calibrator.cancelledByPlayback());
  // Запустить калибровку при идущем транспорте нельзя, но отказ отметку не
  // снимает.
  CHECK_FALSE(bench.calibrator.start(true, waiting(), 0, -50.0, bench.now));
  CHECK(bench.calibrator.cancelledByPlayback());
}

TEST_CASE("Калибровка тренажёра: смена входа кончает шаги и итог, порог не трогает") {
  Bench none;
  none.calibrator.onInputChange();
  CHECK(none.lines.empty());

  Bench steps;
  REQUIRE(steps.start());
  steps.play(noise(1.0, -96.0));
  steps.calibrator.onInputChange();
  CHECK_FALSE(steps.calibrator.session());
  CHECK_FALSE(steps.calibrator.listening());
  CHECK_FALSE(steps.calibrator.cancelledByPlayback());
  CHECK(steps.lines.back() == "calibration: ended");
  CHECK(steps.set.empty());

  Bench result;
  REQUIRE(result.start());
  result.play(mutedQuarters());
  REQUIRE(result.calibrator.finished());
  REQUIRE(result.set.size() == 1);
  result.calibrator.onInputChange();
  CHECK_FALSE(result.calibrator.session());
  CHECK(result.set.size() == 1);
}

TEST_CASE("Калибровка тренажёра: удар по струнам после итога кончает сессию, но не "
          "раньше 5 с") {
  Bench bench;
  REQUIRE(bench.start());
  bench.play(mutedQuarters());
  REQUIRE(bench.calibrator.finished());

  // Удар сразу после итога сессию не кончает.
  bench.play(strike());
  REQUIRE(bench.calibrator.session());

  bench.play(noise(5.0, -96.0, 6));
  REQUIRE(bench.calibrator.session());
  bench.play(strike());
  CHECK_FALSE(bench.calibrator.session());
  CHECK(bench.logged("calibration: closed by sound after the result"));
  // Порог поставлен однажды — итогом, а не концом сессии.
  CHECK(bench.set.size() == 1);
}

TEST_CASE("Калибровка тренажёра: конец сессии порог не трогает") {
  Bench bench;
  bench.calibrator.end();
  CHECK(bench.lines.empty());
  REQUIRE(bench.start());
  bench.calibrator.end();
  CHECK_FALSE(bench.calibrator.session());
  CHECK_FALSE(bench.calibrator.listening());
  CHECK(bench.lines.back() == "calibration: ended");
  CHECK(bench.tick().changed == false);
  CHECK(bench.set.empty());
}

TEST_CASE("Калибровка тренажёра: этапы сессии") {
  Bench bench;
  REQUIRE(bench.start());
  // До первого звука — шаг тишины.
  CHECK(phaseOf(*bench.calibrator.session()) == CalibrationPhase::Silence);
  bench.play(noise(1.0, -96.0));
  CHECK(phaseOf(*bench.calibrator.session()) == CalibrationPhase::Silence);
  bench.play(noise(2.5, -96.0, 4));
  CHECK(phaseOf(*bench.calibrator.session()) == CalibrationPhase::Notes);
  CHECK_FALSE(bench.calibrator.finished());

  bench.play(silence(15.5));
  CHECK(phaseOf(*bench.calibrator.session()) == CalibrationPhase::Result);
  CHECK(bench.calibrator.finished());

  Bench quiet;
  REQUIRE(quiet.start());
  quiet.now += 2s;
  quiet.tick();
  CHECK(phaseOf(*quiet.calibrator.session()) == CalibrationPhase::NoSound);
  CHECK(quiet.calibrator.finished());
}

TEST_CASE("Калибровка тренажёра: порог для «Apply anyway» — у перегруза, прижатый") {
  Bench clipping;
  REQUIRE(clipping.start());
  CHECK_FALSE(clipping.calibrator.anywaySilenceDb());
  clipping.play(clippingPlucks());
  const auto result = *clipping.calibrator.session()->calibration->result();
  REQUIRE(result.verdict == Verdict::Clipping);
  CHECK(clipping.calibrator.anywaySilenceDb() == std::clamp(result.thresholdDb, -90.0, -10.0));

  // Удачная калибровка порог поставила сама, итог без нот порога не даёт.
  Bench good;
  REQUIRE(good.start());
  good.play(mutedQuarters());
  REQUIRE(good.calibrator.session()->calibration->result()->verdict == Verdict::Good);
  CHECK_FALSE(good.calibrator.anywaySilenceDb());

  Bench empty;
  REQUIRE(empty.start());
  empty.play(silence(3.1 + 15.5));
  REQUIRE(empty.calibrator.finished());
  CHECK_FALSE(empty.calibrator.anywaySilenceDb());

  const Bench none;
  CHECK_FALSE(none.calibrator.anywaySilenceDb());
}

TEST_CASE("Калибровка тренажёра: середина за краем диапазона порога — на краю") {
  // Шум −130 dBFS калибровка меряет на своём дне, −120 dBFS; щелчки около
  // −62 dBFS дают середину ниже −90.
  Bench bench;
  REQUIRE(bench.start());
  const std::size_t first = samplesOf(3.6);
  std::vector<Pluck> plucks = evenPlucks(first, kSampleRate / 2.0, 8, {110.0, 147.0}, 0.001);
  plucks.push_back(Pluck{.start = plucks.back().start + samplesOf(0.4), .amplitude = 0.0});
  bench.play(render(plucks, first + samplesOf(4.0 + 2.0), SynthSettings{.noiseDb = -130.0}));

  REQUIRE(bench.calibrator.finished());
  const auto result = *bench.calibrator.session()->calibration->result();
  REQUIRE(result.verdict == Verdict::Good);
  REQUIRE(result.thresholdDb < -90.0);
  CHECK(bench.set == std::vector<double>{-90.0});
  CHECK(bench.logged("calibration: silence -50 -> -90 dBFS"));
}
