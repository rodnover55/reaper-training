#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_ShowConsoleMsg
#define REAPERAPI_WANT_GetInputOutputLatency
#define REAPERAPI_WANT_get_config_var
#define REAPERAPI_WANT_projectconfig_var_getoffs
#define REAPERAPI_WANT_projectconfig_var_addr
#define REAPERAPI_WANT_EnumProjects
#define REAPERAPI_WANT_GetSelectedMediaItem
#define REAPERAPI_WANT_GetActiveTake
#define REAPERAPI_WANT_GetMediaItemInfo_Value
#define REAPERAPI_WANT_GetMediaItemTakeInfo_Value
#define REAPERAPI_WANT_SetMediaItemTakeInfo_Value
#define REAPERAPI_WANT_GetMediaItemTake_Source
#define REAPERAPI_WANT_GetMediaSourceNumChannels
#define REAPERAPI_WANT_CreateTakeAudioAccessor
#define REAPERAPI_WANT_DestroyAudioAccessor
#define REAPERAPI_WANT_GetAudioAccessorStartTime
#define REAPERAPI_WANT_GetAudioAccessorEndTime
#define REAPERAPI_WANT_GetAudioAccessorSamples
#define REAPERAPI_WANT_Master_GetPlayRate

#include "debug_actions.hpp"

#include <reaper_plugin_functions.h>

#include "journal.hpp"
#include "trainer.hpp"
#include "window.hpp"

#include "training/onset/detector.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace training::reaper {

#ifdef TRAINING_DEBUG_BUILD

namespace {

int (*hostRegister)(const char *name, void *infostruct) = nullptr;

/// Пишет строку в консоль REAPER и в журнал.
void say(const std::string &line) {
  journal("console: {}", line);
  ShowConsoleMsg((line + "\n").c_str());
}

/// Адрес и размер переменной настроек REAPER по имени: сначала среди настроек
/// текущего проекта, потом среди общих. Пустой адрес — такой переменной нет.
void *configVarAddress(const char *name, int &size) {
  size = 0;
  if (!name)
    return nullptr;

  const int offset = projectconfig_var_getoffs(name, &size);
  if (size > 0)
    if (void *address = projectconfig_var_addr(EnumProjects(-1, nullptr, 0), offset))
      return address;

  size = 0;
  return get_config_var(name, &size);
}

/// Читает переменную настроек REAPER по имени и при `set` записывает в неё
/// `value`. Переменная в 4 байта считается целой (`value` округляется), в 8 —
/// дробной, в 1 — байтом. Скрипты проверок ставят ею то, для чего нет
/// действий: ручные поправки задержки, count-in, вопрос о сохранении записи.
///
/// @return прежнее значение; NaN, если переменной с таким именем нет.
double configVar(const char *name, double value, bool set) {
  int size = 0;
  void *address = configVarAddress(name, size);
  if (!address || size <= 0)
    return std::numeric_limits<double>::quiet_NaN();

  double previous = std::numeric_limits<double>::quiet_NaN();
  if (size == sizeof(double)) {
    std::memcpy(&previous, address, sizeof(double));
    if (set)
      std::memcpy(address, &value, sizeof(double));
  } else if (size == sizeof(std::int32_t)) {
    std::int32_t current = 0;
    std::memcpy(&current, address, sizeof(current));
    previous = current;
    if (set) {
      const auto next = static_cast<std::int32_t>(std::lround(value));
      std::memcpy(address, &next, sizeof(next));
    }
  } else if (size == 1) {
    std::uint8_t current = 0;
    std::memcpy(&current, address, sizeof(current));
    previous = current;
    if (set) {
      const auto next = static_cast<std::uint8_t>(std::lround(value));
      std::memcpy(address, &next, sizeof(next));
    }
  }

  journal("config var {}: {} -> {}", name, previous, set ? value : previous);
  return previous;
}

void *configVarVararg(void **args, int count) {
  thread_local double result = 0.0;
  if (count < 3)
    return &result;

  const auto *name = static_cast<const char *>(args[0]);
  const double value = args[1] ? *static_cast<const double *>(args[1]) : 0.0;
  const bool set = reinterpret_cast<std::intptr_t>(args[2]) != 0;
  result = configVar(name, value, set);
  return &result;
}

const char *const kConfigVarDef =
    "double\0const char*,double,bool\0name,value,set\0"
    "reaper-training (debug): read and optionally set a REAPER config variable, returns the "
    "previous value";

/// Меняет настройки тренажёра так же, как окно, и показывает их в окне.
///
/// @param mode режим: 1, 2, 3, 4 или 6 нот на удар.
/// @param toleranceMs допуск, мс.
/// @param channel входной канал, 1 — первый.
/// @param silenceDb порог тишины, dBFS.
void setSettings(int mode, double toleranceMs, int channel, double silenceDb) {
  trainer().setSettings({.mode = static_cast<grid::Mode>(mode),
                         .toleranceMs = toleranceMs,
                         .channel = channel - 1,
                         .silenceDb = silenceDb});
  showSettings();
  refreshWindow();
  journal("script settings: mode {}, tolerance {} ms, channel {}, silence {} dBFS", mode,
          toleranceMs, channel, silenceDb);
}

void *setSettingsVararg(void **args, int count) {
  if (count < 4)
    return nullptr;

  const auto number = [args](int index) {
    return args[index] ? *static_cast<const double *>(args[index]) : 0.0;
  };
  setSettings(static_cast<int>(reinterpret_cast<std::intptr_t>(args[0])), number(1),
              static_cast<int>(reinterpret_cast<std::intptr_t>(args[2])), number(3));
  return nullptr;
}

const char *const kSetSettingsDef =
    "void\0int,double,int,double\0mode,toleranceMs,channel,silenceDb\0"
    "reaper-training (debug): set trainer settings like the window does; channel 1 is the "
    "first";

/// Меняет порог энергии удара по звучащей струне
/// (`onset::Settings::energyRatio`) — на лету и для сверки с айтемом. В окне
/// его нет: он нужен только для проверок на записях.
///
/// @param energyRatio порог, больше 1.
void setEnergyRatio(double energyRatio) {
  if (!(energyRatio > 1.0)) {
    say(fmt::format("порог энергии должен быть больше 1, а не {}", energyRatio));
    return;
  }

  trainer().input().setEnergyRatio(energyRatio);
  journal("script energy ratio: {}", energyRatio);
}

void *setEnergyRatioVararg(void **args, int count) {
  if (count >= 1)
    setEnergyRatio(args[0] ? *static_cast<const double *>(args[0]) : 0.0);
  return nullptr;
}

const char *const kSetEnergyRatioDef =
    "void\0double\0energyRatio\0"
    "reaper-training (debug): set how much the high-frequency energy must grow for a strike "
    "on a ringing string to count; default 2";

/// Строка от скрипта проверки в журнал: так в журнале видно, где какой
/// сценарий.
void journalFromScript(const char *text) { journal("script: {}", text ? text : ""); }

void *journalFromScriptVararg(void **args, int count) {
  if (count >= 1)
    journalFromScript(static_cast<const char *>(args[0]));
  return nullptr;
}

const char *const kJournalDef =
    "void\0const char*\0text\0reaper-training (debug): write a line into "
    "reaper-training.log";

/// Медиана; пустой набор даёт 0.
double median(std::vector<double> values) {
  if (values.empty())
    return 0.0;

  const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
  std::nth_element(values.begin(), middle, values.end());
  return *middle;
}

/// Ближайшее к `value` значение из отсортированного `sorted`; набор не пуст.
double nearest(const std::vector<double> &sorted, double value) {
  const auto above = std::ranges::lower_bound(sorted, value);
  if (above == sorted.begin())
    return *above;
  if (above == sorted.end())
    return sorted.back();

  const double before = *std::prev(above);
  return value - before <= *above - value ? before : *above;
}

/// Начала нот в выделенном айтеме на шкале проекта: тот же детектор, что на
/// лету, по первому каналу активного тейка. Время чтения тейка считается от
/// начала айтема и уже пересчитано к скорости тейка (findings.md, R2), поэтому
/// на время чтения скорость тейка ставится 1 и потом возвращается: так
/// сэмплы идут как в файле. Пусто — айтем не выделен или нот нет.
std::vector<double> itemOnsets(double sampleRate) {
  MediaItem *item = GetSelectedMediaItem(nullptr, 0);
  MediaItem_Take *take = item ? GetActiveTake(item) : nullptr;
  if (!take) {
    say("сверка: выделите айтем со звуком");
    return {};
  }

  const double itemPosition = GetMediaItemInfo_Value(item, "D_POSITION");
  const int channels = std::max(1, GetMediaSourceNumChannels(GetMediaItemTake_Source(take)));
  const double takeRate = GetMediaItemTakeInfo_Value(take, "D_PLAYRATE");
  if (takeRate != 1.0)
    SetMediaItemTakeInfo_Value(take, "D_PLAYRATE", 1.0);

  AudioAccessor *accessor = CreateTakeAudioAccessor(take);
  const double start = GetAudioAccessorStartTime(accessor);
  const double end = GetAudioAccessorEndTime(accessor);

  constexpr int kChunk = 4096;
  const auto rate = static_cast<int>(std::lround(sampleRate));
  std::vector<double> interleaved(static_cast<std::size_t>(kChunk * channels));
  std::vector<float> mono(kChunk);
  std::vector<onset::Onset> found;
  onset::Detector detector(onset::Settings{.sampleRate = sampleRate,
                                           .silenceDb = trainer().input().silenceDb(),
                                           .energyRatio = trainer().input().energyRatio()});

  for (std::int64_t chunk = 0;; ++chunk) {
    const double from = start + static_cast<double>(chunk * kChunk) / sampleRate;
    if (from >= end)
      break;

    std::ranges::fill(interleaved, 0.0);
    (void)GetAudioAccessorSamples(accessor, rate, channels, from, kChunk, interleaved.data());
    for (std::size_t i = 0; i < mono.size(); ++i)
      mono[i] = static_cast<float>(interleaved[i * static_cast<std::size_t>(channels)]);

    detector.process(mono, found);
  }

  DestroyAudioAccessor(accessor);
  if (takeRate != 1.0)
    SetMediaItemTakeInfo_Value(take, "D_PLAYRATE", takeRate);

  // Сэмпл n файла лежит на шкале в начале айтема плюс n / частота / скорость
  // тейка.
  std::vector<double> times;
  times.reserve(found.size());
  for (const onset::Onset &hit : found)
    times.push_back(itemPosition + start + hit.position / sampleRate / takeRate);

  return times;
}

/// Сверка с айтемом (design.md D10): ноты последнего запуска, найденные на
/// лету, против тех же нот в записанном айтеме. Печатает сдвиг «на лету −
/// айтем», остаток после него и задержки, о которых знает REAPER.
void compareWithItem() {
  const std::vector<HookOnset> &run = trainer().lastRun();
  if (run.empty()) {
    say("сверка: на лету ещё ничего не измерено — запишите дубль с плагином");
    return;
  }

  // Скорость проекта считается неизменной с записи дубля: сэмплы идут в
  // реальном времени, на шкалу ложатся с её множителем (design.md D2).
  const double sampleRate = run.front().sampleRate;
  const double rate = Master_GetPlayRate(nullptr);
  say(fmt::format("сверка с айтемом, частота {} Гц, скорость проекта {}, порог тишины {} "
                  "dBFS, порог энергии {}:",
                  sampleRate, rate, trainer().input().silenceDb(),
                  trainer().input().energyRatio()));

  std::vector<double> live;
  live.reserve(run.size());
  for (const HookOnset &hit : run)
    live.push_back(hit.blockPosition + hit.offset / hit.sampleRate * rate);
  std::ranges::sort(live);

  const std::vector<double> item = itemOnsets(sampleRate);
  if (item.empty()) {
    say("  в айтеме нот не найдено");
    return;
  }

  // Сдвиг — медиана разниц с ближайшей нотой на лету: так одна лишняя нота не
  // сбивает оценку. Ноты гитары реже самого сдвига, и ближайшая — своя.
  std::vector<double> differences;
  for (const double time : item) {
    const double difference = nearest(live, time) - time;
    if (std::abs(difference) < 0.5)
      differences.push_back(difference);
  }
  const double shift = median(differences);

  constexpr double kPairWindow = 0.005;
  std::size_t paired = 0;
  double sum = 0.0;
  double largest = 0.0;
  for (const double time : item) {
    const double residual = nearest(live, time + shift) - (time + shift);
    if (std::abs(residual) > kPairWindow)
      continue;

    ++paired;
    sum += residual;
    largest = std::max(largest, std::abs(residual));
  }

  int inputLatency = 0;
  int outputLatency = 0;
  GetInputOutputLatency(&inputLatency, &outputLatency);
  const double driver = static_cast<double>(inputLatency + outputLatency) / sampleRate;

  say(fmt::format("  нот: в айтеме {}, на лету {}, в паре {}; без пары: в айтеме {}, на "
                  "лету {}",
                  item.size(), live.size(), paired, item.size() - paired,
                  live.size() > paired ? live.size() - paired : 0));
  say(fmt::format("  сдвиг «на лету − айтем»: {:.4f} мс шкалы, {:.4f} мс реального времени "
                  "({:.2f} сэмпла)",
                  shift * 1000.0, shift / rate * 1000.0, shift / rate * sampleRate));
  say(fmt::format("  остаток после сдвига: среднее {:.4f} мс, наибольший {:.4f} мс",
                  paired > 0 ? sum / static_cast<double>(paired) * 1000.0 : 0.0,
                  largest * 1000.0));
  say(fmt::format("  задержки REAPER: вход {}, выход {} сэмплов, вместе {:.4f} мс; сдвиг в "
                  "реальном времени − задержки = {:.4f} мс",
                  inputLatency, outputLatency, driver * 1000.0,
                  (shift / rate - driver) * 1000.0));
}

struct DebugAction {
  custom_action_register_t registration;
  std::function<void()> run;
  int command = 0;
};

std::array<DebugAction, 1> actions{{
    {{0, "REAPER_TRAINING_DEBUG_COMPARE_ITEM",
      "reaper-training (debug): compare onsets of the last run with the selected item",
      nullptr},
     compareWithItem},
}};

bool onAction(KbdSectionInfo * /*section*/, int command, int /*val*/, int /*val2*/,
              int /*relmode*/, HWND /*hwnd*/) {
  if (command == 0)
    return false;

  for (auto &action : actions) {
    if (action.command == command) {
      journal("debug action {}", action.registration.idStr);
      action.run();
      return true;
    }
  }

  return false;
}

} // namespace

void registerDebugActions(reaper_plugin_info_t *rec) {
  if (!rec || !rec->Register)
    return;

  hostRegister = rec->Register;

  for (auto &action : actions) {
    action.command = hostRegister("custom_action", &action.registration);
    journal("debug action {} -> {}", action.registration.idStr, action.command);
  }

  hostRegister("hookcommand2", reinterpret_cast<void *>(onAction));

  hostRegister("API_TrainingDebug_ConfigVar", reinterpret_cast<void *>(configVar));
  hostRegister("APIvararg_TrainingDebug_ConfigVar", reinterpret_cast<void *>(configVarVararg));
  hostRegister("APIdef_TrainingDebug_ConfigVar", const_cast<char *>(kConfigVarDef));

  hostRegister("API_TrainingDebug_SetSettings", reinterpret_cast<void *>(setSettings));
  hostRegister("APIvararg_TrainingDebug_SetSettings",
               reinterpret_cast<void *>(setSettingsVararg));
  hostRegister("APIdef_TrainingDebug_SetSettings", const_cast<char *>(kSetSettingsDef));

  hostRegister("API_TrainingDebug_SetEnergyRatio", reinterpret_cast<void *>(setEnergyRatio));
  hostRegister("APIvararg_TrainingDebug_SetEnergyRatio",
               reinterpret_cast<void *>(setEnergyRatioVararg));
  hostRegister("APIdef_TrainingDebug_SetEnergyRatio", const_cast<char *>(kSetEnergyRatioDef));

  hostRegister("API_TrainingDebug_Journal", reinterpret_cast<void *>(journalFromScript));
  hostRegister("APIvararg_TrainingDebug_Journal",
               reinterpret_cast<void *>(journalFromScriptVararg));
  hostRegister("APIdef_TrainingDebug_Journal", const_cast<char *>(kJournalDef));
}

void unregisterDebugActions() {
  if (!hostRegister)
    return;

  hostRegister("-API_TrainingDebug_ConfigVar", reinterpret_cast<void *>(configVar));
  hostRegister("-API_TrainingDebug_SetSettings", reinterpret_cast<void *>(setSettings));
  hostRegister("-API_TrainingDebug_SetEnergyRatio", reinterpret_cast<void *>(setEnergyRatio));
  hostRegister("-API_TrainingDebug_Journal", reinterpret_cast<void *>(journalFromScript));
  hostRegister("-hookcommand2", reinterpret_cast<void *>(onAction));

  for (auto &action : actions)
    hostRegister("-custom_action", &action.registration);
}

#else

void registerDebugActions(reaper_plugin_info_t * /*rec*/) {}

void unregisterDebugActions() {}

#endif

} // namespace training::reaper
