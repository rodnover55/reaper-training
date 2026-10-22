#include <doctest/doctest.h>

#include "journal.hpp"
#include "synth.hpp"
#include "wav.hpp"

#include "training/core/calibration/calibrator.hpp"
#include "training/reaper/calibration.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using training::core::Interval;
using training::core::Moment;
using training::core::Range;
using training::core::SampleRate;
using training::core::calibration::NotesStep;
using training::core::calibration::Step;
using training::reaper::Calibration;
using training::reaper::CalibrationJournal;
using training::test::evenPlucks;
using training::test::Pluck;
using training::test::readWav;
using training::test::render;
using training::test::SynthSettings;

namespace {

constexpr double kSampleRate = 48000.0;

/// Допустимый порог тишины тестов, dBFS: от −90 до −10.
constexpr Range<double> kSilenceRange{.min = -90.0, .max = -10.0};

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

/// Калибровка с журналом: звук подаётся кусками, как его отдаёт звуковая
/// карта.
struct Bench {
  std::vector<std::string> lines;
  CalibrationJournal journal{[this](std::string_view line) { lines.emplace_back(line); }};
  // Транспорт под частоту последнего куска — как у связки
  // (`training::reaper::Trainer::syncTransport`).
  std::unique_ptr<training::core::Transport> transport;
  Calibration calibration{kSilenceRange, &journal};
  /// Время первого сэмпла следующего куска во времени ядра.
  Moment stream = Moment() + 5 * Interval::kSecond;
  /// Пороги, которые калибровка велела поставить: порог удачного итога
  /// (`Calibration::silenceDb`) — на куске, с которым итог появился.
  std::vector<double> set;

  Bench() = default;
  Bench(const Bench &) = delete;
  Bench &operator=(const Bench &) = delete;
  Bench(Bench &&) = delete;
  Bench &operator=(Bench &&) = delete;
  ~Bench() = default;

  /// Начинает сессию с порогом до калибровки −50 dBFS, как связка.
  void start() {
    calibration.start();
    journal.started(0, "Input 1", -50.0);
  }

  /// Подаёт кусок `samples` частоты `sampleRate` с временем первого сэмпла
  /// `stream`; следующий кусок идёт вплотную за ним.
  void feed(std::span<const float> samples, double sampleRate = kSampleRate) {
    const SampleRate rate = *SampleRate::create(sampleRate);
    if (!transport || transport->sampleRate() != rate) {
      transport = std::make_unique<training::core::Transport>(
          training::core::Settings(
              *training::core::HitWindow::create(Interval(), 10 * Interval::kMillisecond),
              -50.0),
          rate);
      calibration.setTransport(transport.get());
    }
    const bool finished = calibration.finished();
    calibration.setSampleRate(rate);
    calibration.receive({.samples = samples, .start = stream});
    stream += static_cast<double>(samples.size()) / sampleRate * Interval::kSecond;
    if (!finished && calibration.finished())
      if (const std::optional<double> silenceDb = calibration.silenceDb())
        set.push_back(*silenceDb);
  }

  /// Подаёт `signal` кусками по 512 сэмплов частоты `sampleRate`.
  void play(std::span<const float> signal, double sampleRate = kSampleRate) {
    for (std::size_t from = 0; from < signal.size(); from += 512) {
      const std::size_t length = std::min<std::size_t>(512, signal.size() - from);
      feed(signal.subspan(from, length), sampleRate);
    }
  }

  /// Состояние калибратора; сессия и звук в ней обязательны.
  training::core::calibration::Calibrator::State state() const {
    const auto current = calibration.state();
    REQUIRE(current);
    return *current;
  }

  bool logged(std::string_view line) const {
    return std::ranges::find(lines, line) != lines.end();
  }
};

} // namespace

TEST_CASE("Калибровка связки: запуск начинает сессию, новый запуск кончает прежнюю") {
  Bench bench;
  bench.calibration.start();
  CHECK(bench.calibration.listening());
  CHECK_FALSE(bench.calibration.state());
  CHECK_FALSE(bench.calibration.finished());
  CHECK(bench.lines.empty());

  // Новый запуск кончает прежнюю сессию.
  bench.calibration.start();
  CHECK(bench.lines == std::vector<std::string>{"calibration: ended"});
  CHECK(bench.calibration.listening());
}

TEST_CASE("Калибровка связки: глушёные ноты DI-гитары ставят порог") {
  Bench bench;
  bench.start();
  bench.play(mutedQuarters());

  const auto state = bench.state();
  CHECK(state.step == Step::Done);
  CHECK(state.verdict == NotesStep::Verdict::Good);
  CHECK(bench.calibration.finished());
  CHECK(bench.calibration.listening());

  // Порог велено поставить один раз — тот, что посчитала калибровка.
  CHECK(bench.calibration.silenceDb() == state.thresholdDb);
  CHECK(bench.set == std::vector<double>{state.thresholdDb});
  CHECK(bench.logged("calibration: 48000 Hz, silence step"));
  REQUIRE(state.attacksDb.size() == 8);
  CHECK(bench.logged("calibration: note 8 attack " +
                     fmt::format("{:.1f}", state.attacksDb[7]) + " dBFS"));
  CHECK(bench.logged(fmt::format("calibration: silence -50 -> {} dBFS", state.thresholdDb)));
}

TEST_CASE("Калибровка связки: смена частоты начинает заново, после итога — нет") {
  Bench bench;
  bench.start();
  bench.play(noise(1.0, -96.0));
  bench.play(noise(0.5, -96.0), 44100.0);

  CHECK(bench.state().step == Step::Silence);
  CHECK(bench.state().silenceLeft / Interval::kSecond > 2.5);
  CHECK(bench.logged("calibration: 44100 Hz, silence step"));

  Bench done;
  done.start();
  done.play(mutedQuarters());
  REQUIRE(done.calibration.finished());
  const std::size_t lines = done.lines.size();
  done.play(noise(0.5, -96.0), 44100.0);
  CHECK(done.state().step == Step::Done);
  CHECK(done.lines.size() == lines);
}

TEST_CASE("Калибровка связки: пропущенный звук — тишина, шаг тишины идёт дальше") {
  Bench bench;
  bench.start();
  bench.play(noise(2.0, -96.0));
  const double before = bench.state().silenceLeft / Interval::kSecond;
  REQUIRE(before < 1.5);

  // Звук пропал на 0.1 с: следующий кусок начинается позже конца прошлого.
  const std::vector<float> more = noise(0.1, -96.0, 4);
  bench.stream += 100 * Interval::kMillisecond;
  bench.feed(std::span(more).first(512));
  CHECK(bench.state().silenceLeft / Interval::kSecond < before - 0.1);
}

TEST_CASE("Калибровка связки: звук в тишине отмечается, начало шага — нет") {
  Bench quiet;
  quiet.start();
  quiet.play(noise(2.0, -96.0));
  CHECK_FALSE(quiet.calibration.heardSound());
  CHECK_FALSE(quiet.logged("calibration: sound during silence, silence step counts again"));

  Bench bench;
  bench.start();
  bench.play(noise(1.0, -96.0));
  bench.play(strike());
  bench.play(noise(0.3, -96.0, 7));
  CHECK(bench.calibration.heardSound());
  CHECK(bench.state().sinceSound / Interval::kSecond < 1.0);
  CHECK(bench.logged("calibration: sound during silence, silence step counts again"));

  // Новая сессия звука ещё не слышала.
  bench.start();
  CHECK_FALSE(bench.calibration.heardSound());
}

TEST_CASE("Калибровка связки: тишина — итог без нот, порог не меняется") {
  Bench bench;
  bench.start();
  bench.play(silence(3.2 + 15.5));

  REQUIRE(bench.calibration.finished());
  CHECK(bench.state().verdict == NotesStep::Verdict::NotEnoughNotes);
  CHECK(bench.state().attacksDb.empty());
  CHECK_FALSE(bench.calibration.silenceDb());
  CHECK(bench.set.empty());
  // «Apply anyway» без нот ничего не ставит, и сессия остаётся.
  CHECK_FALSE(bench.calibration.applyAnyway());
  CHECK(bench.calibration.listening());
}

TEST_CASE("Калибровка связки: перегруз — порог только по «Apply anyway»") {
  Bench bench;
  bench.start();
  bench.play(clippingPlucks());

  REQUIRE(bench.calibration.finished());
  const auto state = bench.state();
  CHECK(state.verdict == NotesStep::Verdict::Clipping);
  CHECK_FALSE(bench.calibration.silenceDb());
  CHECK(bench.set.empty());

  bench.lines.clear();
  CHECK(bench.calibration.applyAnyway() == std::clamp(state.thresholdDb, -90.0, -10.0));
  CHECK_FALSE(bench.calibration.listening());
  REQUIRE(bench.lines.size() == 2);
  CHECK(bench.lines[0] == fmt::format("calibration: applied anyway, silence {} dBFS",
                                      std::clamp(state.thresholdDb, -90.0, -10.0)));
  CHECK(bench.lines[1] == "calibration: ended");
}

TEST_CASE("Калибровка связки: запуск транспорта прерывает шаги и закрывает итог") {
  Bench steps;
  steps.start();
  steps.play(noise(1.0, -96.0));
  steps.calibration.onStart();
  CHECK_FALSE(steps.calibration.listening());
  CHECK(steps.calibration.cancelledByPlayback());
  CHECK(steps.logged("calibration: cancelled by playback"));
  CHECK(steps.lines.back() == "calibration: ended");
  steps.calibration.onStop();
  CHECK_FALSE(steps.calibration.cancelledByPlayback());

  Bench result;
  result.start();
  result.play(mutedQuarters());
  REQUIRE(result.calibration.finished());
  result.calibration.onStart();
  CHECK_FALSE(result.calibration.listening());
  CHECK_FALSE(result.calibration.cancelledByPlayback());
  CHECK(result.logged("calibration: closed by playback"));
}

TEST_CASE("Калибровка связки: смена входа кончает шаги и итог, порог не трогает") {
  Bench none;
  none.calibration.onInputChange();
  CHECK(none.lines.empty());

  Bench steps;
  steps.start();
  steps.play(noise(1.0, -96.0));
  steps.calibration.onInputChange();
  CHECK_FALSE(steps.calibration.listening());
  CHECK_FALSE(steps.calibration.state());
  CHECK_FALSE(steps.calibration.cancelledByPlayback());
  CHECK(steps.lines.back() == "calibration: ended");
  CHECK(steps.set.empty());

  Bench result;
  result.start();
  result.play(mutedQuarters());
  REQUIRE(result.calibration.finished());
  REQUIRE(result.set.size() == 1);
  result.calibration.onInputChange();
  CHECK_FALSE(result.calibration.listening());
  CHECK(result.set.size() == 1);
}

TEST_CASE("Калибровка связки: удар по струнам после итога кончает сессию, но не раньше "
          "5 с") {
  Bench bench;
  bench.start();
  bench.play(mutedQuarters());
  REQUIRE(bench.calibration.finished());

  // Удар сразу после итога сессию не кончает.
  bench.play(strike());
  REQUIRE(bench.calibration.listening());

  bench.play(noise(5.0, -96.0, 6));
  REQUIRE(bench.calibration.listening());
  bench.play(strike());
  CHECK_FALSE(bench.calibration.listening());
  CHECK(bench.logged("calibration: closed by sound after the result"));
  CHECK(bench.lines.back() == "calibration: ended");
  // Порог поставлен однажды — итогом, а не концом сессии.
  CHECK(bench.set.size() == 1);
}

TEST_CASE("Калибровка связки: конец сессии порог не трогает") {
  Bench bench;
  bench.calibration.end();
  CHECK(bench.lines.empty());
  bench.start();
  bench.calibration.end();
  CHECK_FALSE(bench.calibration.listening());
  CHECK(bench.lines.back() == "calibration: ended");
  CHECK_FALSE(bench.calibration.silenceDb());
  CHECK(bench.set.empty());
}

TEST_CASE("Калибровка связки: шаги калибратора по ходу сессии") {
  Bench bench;
  bench.start();
  // До первого звука состояния калибратора нет.
  CHECK_FALSE(bench.calibration.state());
  bench.play(noise(1.0, -96.0));
  CHECK(bench.state().step == Step::Silence);
  bench.play(noise(2.5, -96.0, 4));
  CHECK(bench.state().step == Step::Notes);
  CHECK(bench.logged(
      fmt::format("calibration: noise {:.1f} dBFS, notes step", bench.state().noiseDb)));
  CHECK_FALSE(bench.calibration.finished());

  bench.play(silence(15.5));
  CHECK(bench.state().step == Step::Done);
  CHECK(bench.calibration.finished());
}

TEST_CASE("Калибровка связки: порог для «Apply anyway» — у перегруза, прижатый") {
  Bench clipping;
  clipping.start();
  CHECK_FALSE(clipping.calibration.anywaySilenceDb());
  clipping.play(clippingPlucks());
  const auto state = clipping.state();
  REQUIRE(state.verdict == NotesStep::Verdict::Clipping);
  CHECK(clipping.calibration.anywaySilenceDb() == std::clamp(state.thresholdDb, -90.0, -10.0));

  // Удачная калибровка порог поставила сама, итог без нот порога не даёт.
  Bench good;
  good.start();
  good.play(mutedQuarters());
  REQUIRE(good.state().verdict == NotesStep::Verdict::Good);
  CHECK_FALSE(good.calibration.anywaySilenceDb());

  Bench empty;
  empty.start();
  empty.play(silence(3.2 + 15.5));
  REQUIRE(empty.calibration.finished());
  CHECK_FALSE(empty.calibration.anywaySilenceDb());

  const Bench none;
  CHECK_FALSE(none.calibration.anywaySilenceDb());
}

TEST_CASE("Калибровка связки: середина за краем диапазона порога — на краю") {
  // Шум −130 dBFS калибровка меряет на своём дне, −120 dBFS; щелчки около
  // −62 dBFS дают середину ниже −90.
  Bench bench;
  bench.start();
  const std::size_t first = samplesOf(3.6);
  std::vector<Pluck> plucks = evenPlucks(first, kSampleRate / 2.0, 8, {110.0, 147.0}, 0.001);
  plucks.push_back(Pluck{.start = plucks.back().start + samplesOf(0.4), .amplitude = 0.0});
  bench.play(render(plucks, first + samplesOf(4.0 + 2.0), SynthSettings{.noiseDb = -130.0}));

  REQUIRE(bench.calibration.finished());
  const auto state = bench.state();
  REQUIRE(state.verdict == NotesStep::Verdict::Good);
  REQUIRE(state.thresholdDb < -90.0);
  CHECK(bench.set == std::vector<double>{-90.0});
  CHECK(bench.logged("calibration: silence -50 -> -90 dBFS"));
}
