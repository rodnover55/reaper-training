#include <doctest/doctest.h>

#include "grid_timeline.hpp"

#include "training/core/hit_window.hpp"
#include "training/core/training/state/timeline.hpp"
#include "training/render/main/bar_rows.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using training::core::HitWindow;
using training::core::Interval;
using training::core::Moment;
using training::core::training::state::Bar;
using training::core::training::state::Beat;
using training::core::training::state::Timeline;
using training::render::BarRow;
using training::render::BarRows;
using training::render::BarView;
using training::render::Tone;
using training::test::BeatTimeline;
using training::test::coreBars;
using training::test::MapTimeline;
using training::test::msOf;
using training::test::SteadyTimeline;

namespace {

/// Окно попадания `offsetMs` ± `toleranceMs`.
HitWindow windowOf(double offsetMs, double toleranceMs) {
  return *HitWindow::create(msOf(offsetMs), msOf(toleranceMs));
}

/// Карта темпа тестов: 120 BPM, 4/4, удар — 0.5 с.
const SteadyTimeline &steady() {
  static const SteadyTimeline timeline(120.0);
  return timeline;
}

/// Окно попадания тестов, если не сказано другое: 0 ± 10 мс.
constexpr HitWindow kWindow = *HitWindow::create(Interval(), 10 * Interval::kMillisecond);

/// Окно попадания по центру клика с допуском `toleranceMs`.
HitWindow tolerance(double toleranceMs) { return windowOf(0.0, toleranceMs); }

/// Такты запуска по карте `map`: предтакт — такт `first − 1`, затем такты
/// `first`…`last` с `perBeat` долями на удар. Такт карты `first + k` — в
/// списке под номером `k + 1`.
Timeline runOf(std::int64_t first, std::int64_t last, int perBeat = 1,
               const BeatTimeline &map = steady()) {
  return coreBars(map, first - 1, last, perBeat);
}

/// Такт `bar` с подписью `name` и началом `start`: конец, размер и темп те же, доли —
/// те, что не раньше `start`; нот нет.
Bar rebuilt(const Bar &bar, std::string name, Moment start) {
  Bar result(std::move(name), start, bar.end(), bar.meter(), bar.tempo().at(Beat()));
  for (const auto &[beat, tempo] : bar.tempo())
    result.changeTempo(beat, tempo);
  for (const auto &[beat, at] : bar.divisions())
    result.addDivision(beat, at);
  return result;
}

/// Такты `timeline`, в которых такт номер `index` списка заменён тактом `bar`.
Timeline replaced(const Timeline &timeline, std::size_t index, const Bar &bar) {
  Timeline result;
  for (std::size_t k = 0; k < timeline.bars().size(); ++k)
    result.add(k == index ? bar : timeline.bars()[k]);
  return result;
}

/// Кладёт в долю `place` такта номер `index` списка ноту с разницей `ms` мс
/// и оценкой по окну `window`.
void note(Timeline &timeline, std::size_t index, double place, double ms,
          const HitWindow &window = kWindow) {
  const Interval deviation = msOf(ms);
  REQUIRE(timeline.addNote({.bar = index, .division = Beat(place)},
                           {.deviation = deviation, .hit = window.hit(deviation)}));
}

/// Что показывают строки области строк тактов (`BarRows::setBars`) сверху
/// вниз при слышимой позиции на ударе `beats` от начала проекта карты `map`.
std::vector<BarView> present(const Timeline &timeline, double beats, bool playing = true,
                             const HitWindow &window = kWindow,
                             const BeatTimeline &map = steady()) {
  BarRows rows;
  rows.setBars(timeline, map.beatTime(beats), playing, window);
  std::vector<BarView> views;
  for (const BarRow &row : rows.rows())
    views.push_back(row.view());
  return views;
}

/// Тексты значений строки по порядку.
std::vector<std::string> texts(const BarView &view) {
  std::vector<std::string> result;
  result.reserve(view.values.size());
  for (const auto &value : view.values)
    result.push_back(value.cell.text);
  return result;
}

/// Подписи строк сверху вниз.
std::vector<std::string> labels(const std::vector<BarView> &views) {
  std::vector<std::string> result;
  result.reserve(views.size());
  for (const BarView &view : views)
    result.push_back(view.label);
  return result;
}

} // namespace

TEST_CASE("показ тактов: шестнадцатые в 4/4 — четыре значения на ударах и двенадцать между") {
  Timeline run = runOf(0, 0, 4);
  for (int node = 0; node < 16; ++node)
    note(run, 1, node / 4.0, 1.0);

  const auto views = present(run, 3.9);
  REQUIRE(views.size() == 1);
  const BarView &view = views.front();
  CHECK(view.beats == 4);
  CHECK(view.values.size() == 16);
  std::size_t onBeat = 0;
  for (const auto &value : view.values)
    onBeat += value.onBeat ? 1 : 0;
  CHECK(onBeat == 4);
  CHECK(view.values[5].beat == doctest::Approx(1.25));
  CHECK(view.dots.empty());
  CHECK(view.division == 4);
}

TEST_CASE("показ тактов: восьмые в сетке шестнадцатых — доли между ними пустые") {
  Timeline run = runOf(0, 0, 4);
  for (int node = 0; node < 8; ++node)
    note(run, 1, node / 2.0, 2.0);

  const auto views = present(run, 3.9);
  REQUIRE(views.size() == 1);
  REQUIRE(views.front().values.size() == 8);
  for (std::size_t i = 0; i < 8; ++i)
    CHECK(views.front().values[i].beat == doctest::Approx(static_cast<double>(i) / 2.0));
}

TEST_CASE("показ тактов: пропуск на ударе — точка, лишняя нота — ближайшая со звёздочкой") {
  Timeline run = runOf(0, 0, 2);
  note(run, 1, 0.5, -20.0);
  note(run, 1, 0.5, 3.0);
  note(run, 1, 1.0, 1.0);

  const BarView view = present(run, 1.9).front();
  CHECK(view.dots == std::vector<int>{0, 2, 3});
  CHECK(texts(view) == std::vector<std::string>{"+3*", "+1"});
  CHECK_FALSE(view.values[0].onBeat);
}

TEST_CASE("показ тактов: цвет значения — по оценке ноты, а не по текущему окну") {
  Timeline run = runOf(0, 0, 2);
  note(run, 1, 0.0, 13.0, tolerance(10.0));
  note(run, 1, 0.5, 13.0, tolerance(10.0));
  note(run, 1, 1.0, 13.0, tolerance(20.0));
  note(run, 1, 1.5, 13.0, tolerance(20.0));

  const BarView view = present(run, 1.6, true, tolerance(20.0)).front();
  REQUIRE(view.values.size() == 4);
  CHECK(view.values[0].cell.tone == Tone::Bad);
  CHECK(view.values[1].cell.tone == Tone::Bad);
  CHECK(view.values[2].cell.tone == Tone::Good);
  CHECK(view.values[3].cell.tone == Tone::Good);
}

TEST_CASE("показ тактов: у верхней строки текущий удар и нет среднего") {
  Timeline run = runOf(0, 1);
  note(run, 1, 0.0, -4.0);
  note(run, 1, 1.0, 2.0);
  note(run, 1, 2.0, 6.0);
  note(run, 1, 3.0, 4.0);
  note(run, 2, 0.0, 1.0);
  note(run, 2, 1.0, 2.0);

  const auto views = present(run, 5.2);
  REQUIRE(views.size() == 2);
  CHECK(views[0].currentBeat == 1);
  CHECK_FALSE(views[0].mean);
  CHECK_FALSE(views[1].currentBeat);
  REQUIRE(views[1].mean);
  REQUIRE(views[1].spread);
  CHECK(views[1].mean->text == "+2.0");
  CHECK(views[1].mean->tone == Tone::Good);
  CHECK(views[1].spread->text == "3.7");
  CHECK(views[1].spread->tone == Tone::Neutral);

  // Без воспроизведения текущего удара нет, а у верхней строки по-прежнему
  // нет среднего.
  const auto stopped = present(run, 5.2, false);
  REQUIRE(stopped.size() == 2);
  CHECK_FALSE(stopped.front().currentBeat);
  CHECK_FALSE(stopped.front().mean);
  CHECK(stopped.back().mean);
}

TEST_CASE("показ тактов: подпись — имя такта «10000»") {
  const Timeline run = runOf(9999, 9999);
  const auto views = present(run, (10000 - 1) * 4.0 + 0.1);
  REQUIRE(views.size() == 1);
  CHECK(views.front().label == "10000");
  CHECK(views.front().dots == std::vector<int>{0, 1, 2, 3});
}

TEST_CASE("показ тактов: кегль — по наибольшему числу долей на одном ударе") {
  Timeline run = runOf(0, 0, 2);
  Bar bar = run.bars()[1];
  for (int node = 1; node < 6; ++node)
    bar.addDivision(Beat(1.0 + node / 6.0), steady().beatTime(1.0 + node / 6.0));
  run = replaced(run, 1, bar);
  CHECK(present(run, 0.1).front().division == 6);
  CHECK(present(runOf(0, 0, 2), 0.1).front().division == 2);
}

TEST_CASE("показ тактов: маркеры смен темпа и размера — против прошлого такта списка") {
  const MapTimeline timeline({
      {.meter = {.beats = 4, .unit = 4}, .bpm = 120.0, .inside = {}},
      {.meter = {.beats = 4, .unit = 4}, .bpm = 150.0, .inside = {{2.0, 100.0}}},
      {.meter = {.beats = 7, .unit = 8}, .bpm = 100.0, .inside = {}},
      {.meter = {.beats = 3, .unit = 4}, .bpm = 120.0, .inside = {}},
      {.meter = {.beats = 4, .unit = 4}, .bpm = 150.0, .inside = {}},
      {.meter = {.beats = 4, .unit = 4}, .bpm = 120.5, .inside = {}},
  });

  // Ноты в каждом такте: пустые строки свернулись бы.
  Timeline run = runOf(0, 5, 1, timeline);
  for (std::size_t index = 1; index <= 6; ++index)
    note(run, index, 0.0, 1.0);

  const auto views = present(run, 22.1, true, kWindow, timeline);
  REQUIRE(views.size() == 6);

  // Строки сверху вниз: последний такт первым.
  REQUIRE(views[4].marks.size() == 2);
  CHECK(views[4].marks[0].beat == 0.0);
  CHECK(views[4].marks[0].text == "120 → 150");
  CHECK(views[4].marks[1].beat == 2.0);
  CHECK(views[4].marks[1].text == "150 → 100");

  REQUIRE(views[3].marks.size() == 1);
  CHECK(views[3].marks[0].text == "4/4 → 7/8");
  CHECK(views[3].beats == 7);

  REQUIRE(views[1].marks.size() == 1);
  CHECK(views[1].marks[0].text == "3/4 → 4/4 · 120 → 150");

  REQUIRE(views[0].marks.size() == 1);
  CHECK(views[0].marks[0].text == "150 → 120.5");

  CHECK(views[5].marks.empty());
}

TEST_CASE("показ тактов: отклонение — целые миллисекунды со знаком и цвет по оценке") {
  Timeline run = runOf(0, 0);
  note(run, 1, 0.0, -15.3);
  note(run, 1, 1.0, 5.0);
  note(run, 1, 2.0, 10.0);
  note(run, 1, 3.0, 0.0);

  const BarView view = present(run, 3.9).front();
  CHECK(texts(view) == std::vector<std::string>{"−15", "+5", "+10", "0"});
  CHECK(view.values[0].cell.tone == Tone::Bad);
  CHECK(view.values[1].cell.tone == Tone::Good);
  CHECK(view.values[2].cell.tone == Tone::Good);
  // Смещение 0: «0» — ровно в смещение.
  CHECK(view.values[3].cell.tone == Tone::Target);
}

TEST_CASE("показ тактов: число — до ближайшего, цвет — по оценке ноты") {
  // При допуске 10 мс ±10.4 мс показываются как «+10» и «−10», но они за
  // окном: красные; −0.4 — «0».
  Timeline run = runOf(0, 0);
  note(run, 1, 0.0, 10.4);
  note(run, 1, 1.0, -10.4);
  note(run, 1, 2.0, -0.4);

  const BarView view = present(run, 2.9).front();
  CHECK(texts(view) == std::vector<std::string>{"+10", "−10", "0"});
  CHECK(view.values[0].cell.tone == Tone::Bad);
  CHECK(view.values[1].cell.tone == Tone::Bad);
}

TEST_CASE("показ тактов: отрицательное, нулевое и далёкое среднее") {
  Timeline run = runOf(0, 3);
  const std::vector<std::vector<double>> values{{-1.0, -2.0}, {-1.0, 1.0}, {12.0, 14.0}};
  for (std::size_t i = 0; i < values.size(); ++i) {
    note(run, i + 1, 0.0, values[i][0]);
    note(run, i + 1, 1.0, values[i][1]);
  }

  const auto views = present(run, 12.1);
  REQUIRE(views.size() == 4);
  CHECK(views[3].mean->text == "−1.5");
  CHECK(views[2].mean->text == "0.0");
  CHECK(views[1].mean->text == "+13.0");
  CHECK(views[1].mean->tone == Tone::Bad);
}

TEST_CASE("показ тактов: разница 8 мс — «+8»") {
  Timeline run = runOf(0, 0);
  note(run, 1, 0.0, 8.0);
  CHECK(texts(present(run, 0.2).front()) == std::vector<std::string>{"+8"});
}

TEST_CASE("показ тактов: цвет по окну попадания позади клика") {
  // Смещение +12, допуск 8: окно от +4 до +20.
  const HitWindow behind = windowOf(12.0, 8.0);
  Timeline run = runOf(0, 0, 4);
  note(run, 1, 0.0, 3.0, behind);
  note(run, 1, 0.25, 4.0, behind);
  note(run, 1, 0.5, 13.0, behind);
  note(run, 1, 0.75, 20.0, behind);
  note(run, 1, 1.0, 21.0, behind);
  note(run, 1, 1.25, 12.0, behind);
  note(run, 1, 1.5, 30.0, behind);
  note(run, 1, 1.5, 12.0, behind);

  const BarView view = present(run, 1.9, true, behind).front();
  CHECK(texts(view) ==
        std::vector<std::string>{"+3", "+4", "+13", "+20", "+21", "+12", "+12*"});
  CHECK(view.values[0].cell.tone == Tone::Bad);
  CHECK(view.values[1].cell.tone == Tone::Good);
  CHECK(view.values[2].cell.tone == Tone::Good);
  CHECK(view.values[3].cell.tone == Tone::Good);
  CHECK(view.values[4].cell.tone == Tone::Bad);
  CHECK(view.values[5].cell.tone == Tone::Target);
  CHECK(view.values[6].cell.tone == Tone::Target);
}

TEST_CASE("показ тактов: смещение с половиной — ровно в него оба соседних числа") {
  const HitWindow half = windowOf(7.5, 12.5);
  Timeline run = runOf(0, 0);
  note(run, 1, 0.0, 7.0, half);
  note(run, 1, 1.0, 8.0, half);
  note(run, 1, 2.0, 9.0, half);

  const BarView view = present(run, 2.9, true, half).front();
  CHECK(texts(view) == std::vector<std::string>{"+7", "+8", "+9"});
  CHECK(view.values[0].cell.tone == Tone::Target);
  CHECK(view.values[1].cell.tone == Tone::Target);
  CHECK(view.values[2].cell.tone == Tone::Good);
}

TEST_CASE("показ тактов: смена смещения не меняет показанные значения") {
  Timeline run = runOf(0, 0);
  note(run, 1, 0.0, 13.0, windowOf(0.0, 8.0));
  note(run, 1, 1.0, 12.0, windowOf(12.0, 8.0));
  note(run, 1, 2.0, 12.0, windowOf(10.0, 8.0));

  const BarView view = present(run, 2.9, true, windowOf(10.0, 8.0)).front();
  CHECK(view.values[0].cell.tone == Tone::Bad);
  CHECK(view.values[1].cell.tone == Tone::Target);
  CHECK(view.values[2].cell.tone == Tone::Good);
}

TEST_CASE("показ тактов: среднее, равное смещению, — зелёное, без подложки") {
  const HitWindow behind = windowOf(12.0, 8.0);
  Timeline run = runOf(0, 1);
  note(run, 1, 0.0, 11.0, behind);
  note(run, 1, 1.0, 13.0, behind);

  const auto views = present(run, 4.1, true, behind);
  REQUIRE(views.size() == 2);
  REQUIRE(views[1].mean);
  CHECK(views[1].mean->text == "+12.0");
  CHECK(views[1].mean->tone == Tone::Good);
}

TEST_CASE("показ тактов: среднее красится по текущему окну попадания") {
  Timeline run = runOf(0, 1);
  note(run, 1, 0.0, 14.0);
  note(run, 1, 1.0, 15.0);

  const auto views = present(run, 4.1, true, windowOf(12.0, 8.0));
  REQUIRE(views.size() == 2);
  REQUIRE(views[1].mean);
  CHECK(views[1].mean->text == "+14.5");
  CHECK(views[1].mean->tone == Tone::Good);
  CHECK(views[1].values[0].cell.tone == Tone::Bad);
}

TEST_CASE("показ тактов: подпись — имя такта, имя с «−» — строка затакта") {
  Timeline run = runOf(2, 4);
  run = replaced(run, 1,
                 rebuilt(run.bars()[1],
                         "−"
                         "2",
                         run.bars()[1].start()));
  run = replaced(run, 2,
                 rebuilt(run.bars()[2],
                         "−"
                         "1",
                         run.bars()[2].start()));
  run = replaced(run, 3, rebuilt(run.bars()[3], "5", run.bars()[3].start()));
  // Ноты в каждом такте: пустые строки свернулись бы.
  for (std::size_t index = 1; index <= 3; ++index)
    note(run, index, 0.0, 1.0);

  const auto views = present(run, 16.1);
  REQUIRE(views.size() == 3);
  CHECK(views[0].label == "5");
  CHECK_FALSE(views[0].leadIn);
  CHECK(views[1].label == "−"
                          "1");
  CHECK(views[1].leadIn);
  CHECK(views[2].label == "−"
                          "2");
  CHECK(views[2].leadIn);
}

TEST_CASE("строки тактов: предтакт строкой не показывается") {
  const Timeline run = runOf(1, 2);
  CHECK(present(run, 3.0).empty());
  CHECK(labels(present(run, 4.0)) == std::vector<std::string>{"2"});
}

TEST_CASE("строки тактов: такты, пришедшие заранее, без строк, пока в них не вошли") {
  const Timeline run = runOf(0, 3);
  CHECK(labels(present(run, 1.0)) == std::vector<std::string>{"1"});
  CHECK(labels(present(run, 4.0)) == std::vector<std::string>{"2", "1"});
}

TEST_CASE("строки тактов: восемь строк, самого первого такта нет") {
  Timeline run = runOf(0, 8);
  for (std::size_t index = 1; index <= 9; ++index)
    note(run, index, 0.0, 1.0);
  const auto views = present(run, 32.1);
  REQUIRE(views.size() == 8);
  CHECK(views.front().label == "9");
  CHECK(views.back().label == "2");
}

TEST_CASE("строки тактов: пауза в игре — из пустых тактов виден самый ранний") {
  // Сыграны такты 1 и 6, такты 2–5 пустые, воспроизведение — в такте 6.
  Timeline run = runOf(0, 5);
  note(run, 1, 0.0, 1.0);
  note(run, 6, 0.0, 1.0);
  CHECK(labels(present(run, 20.1)) == std::vector<std::string>{"6", "2", "1"});
}

TEST_CASE("строки тактов: пауза идёт — пустой звучащий такт сверху") {
  Timeline run = runOf(0, 3);
  note(run, 1, 0.0, 1.0);
  CHECK(labels(present(run, 12.1)) == std::vector<std::string>{"4", "2", "1"});
}

TEST_CASE("строки тактов: позднее значение в паузе возвращает строку такта") {
  Timeline run = runOf(0, 3);
  note(run, 1, 0.0, 1.0);
  note(run, 3, 3.0, 2.0);
  CHECK(labels(present(run, 12.1)) == std::vector<std::string>{"4", "3", "2", "1"});
}

TEST_CASE("строки тактов: один пустой такт между сыгранными остаётся") {
  Timeline run = runOf(0, 2);
  note(run, 1, 0.0, 1.0);
  note(run, 3, 0.0, 1.0);
  CHECK(labels(present(run, 8.1)) == std::vector<std::string>{"3", "2", "1"});
}

TEST_CASE("строки тактов: такт, целиком заменённый пришедшими после него, без строки") {
  // Такт 3 пришёл заранее, а петля вернула воспроизведение к такту 2: его
  // место заняли такты 2 и 3 нового прохода.
  Timeline run = runOf(0, 2);
  note(run, 1, 0.0, 1.0);
  note(run, 2, 0.0, 1.0);
  const std::vector<Bar> first = run.bars();
  const Interval shift = steady().beatTime(8.0) - first[2].start();
  for (std::size_t index = 2; index <= 3; ++index) {
    const Bar &pass = first[index];
    Bar again(pass.name(), pass.start() + shift, pass.end() + shift, pass.meter(),
              pass.tempo().at(Beat()));
    for (const auto &[beat, at] : pass.divisions())
      again.addDivision(beat, at + shift);
    run.add(again);
  }
  note(run, 4, 0.0, 1.0);
  CHECK(labels(present(run, 12.1)) == std::vector<std::string>{"3", "2", "2", "1"});
}

TEST_CASE("строки тактов: такт, начатый с середины, — текущий удар до первой доли") {
  // Такт после петли звучит с места 2.0: доли — с удара 3.
  Timeline run = runOf(0, 0);
  run = replaced(run, 1, rebuilt(run.bars()[1], run.bars()[1].name(), steady().beatTime(1.5)));
  CHECK(present(run, 1.7).front().currentBeat == 1);
  CHECK(present(run, 2.2).front().currentBeat == 2);
}
