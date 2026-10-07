// Копия процесса с загруженным расширением завершается.
//
// REAPER на Linux запускает программы через fork, и копия, у которой exec не
// удался, зовёт exit() — так SWELL запускает `pasuspender`, когда его нет в
// системе. exit() вызывает деструкторы статических объектов модуля, а в копии
// есть только поток, вызвавший fork. Тренажёр, который ждал бы там свой
// рабочий поток, вешал копию навсегда, и она держала звуковую карту REAPER.
//
// Тест загружает собранный модуль с поддельным API REAPER, делает fork и
// проверяет, что копия выходит — и сразу через exit(), и через выгрузку
// расширения. Затем расширение выгружается в самом процессе: там остановка
// должна по-прежнему дожидаться своего потока.
//
// Использование: training_fork_exit_test <модуль> <каталог для журнала>

#include <reaper_plugin.h>

#include <dlfcn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>

namespace {

using Entry = int (*)(REAPER_PLUGIN_HINSTANCE, reaper_plugin_info_t *);

std::string resourcePath;

// Функции API, которые расширение зовёт при загрузке и выгрузке, — с точными
// сигнатурами: вызов через указатель чужого типа ловит UBSan.

const char *fakeGetResourcePath() { return resourcePath.c_str(); }

void fakeShowConsoleMsg(const char * /*message*/) {}

const char *fakeGetExtState(const char * /*section*/, const char * /*key*/) { return ""; }

int fakeAudioRegHardwareHook(bool /*isAdd*/, audio_hook_register_t * /*registration*/) {
  return 1;
}

/// История MIDI-входа пуста.
int fakeMidiGetRecentInputEvent(int /*idx*/, char * /*buf*/, int * /*bufSize*/, int * /*ts*/,
                                int * /*devIdx*/, double * /*projPos*/,
                                int * /*projLoopCnt*/) {
  return 0;
}

/// Заглушка для остальных функций API: расширение их только запоминает, а
/// зовёт из звукового потока, таймера и окна, которых в тесте нет. Вызов
/// значит, что тесту не хватает заглушки, — он падает.
void fakeUncalled() {
  (void)std::fputs("Расширение позвало функцию API без заглушки\n", stderr);
  std::abort();
}

void *fakeGetFunc(const char *name) {
  const std::string_view function(name);
  if (function == "GetResourcePath")
    return reinterpret_cast<void *>(fakeGetResourcePath);
  if (function == "ShowConsoleMsg")
    return reinterpret_cast<void *>(fakeShowConsoleMsg);
  if (function == "GetExtState")
    return reinterpret_cast<void *>(fakeGetExtState);
  if (function == "Audio_RegHardwareHook")
    return reinterpret_cast<void *>(fakeAudioRegHardwareHook);
  if (function == "MIDI_GetRecentInputEvent")
    return reinterpret_cast<void *>(fakeMidiGetRecentInputEvent);
  return reinterpret_cast<void *>(fakeUncalled);
}

/// Регистрация действий, таймера и хуков: всё принимается, номер действия — 1.
int fakeRegister(const char * /*name*/, void * /*infostruct*/) { return 1; }

/// Ждёт завершения процесса pid не дольше timeout. Возвращает true, если
/// процесс завершился с кодом 0. Зависший процесс убивает.
bool exitsCleanly(pid_t pid, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    int status = 0;
    if (waitpid(pid, &status, WNOHANG) == pid)
      return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  (void)kill(pid, SIGKILL);
  (void)waitpid(pid, nullptr, 0);
  return false;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    (void)std::fputs("Использование: training_fork_exit_test <модуль> <каталог для журнала>\n",
                     stderr);
    return 2;
  }
  resourcePath = argv[2];

  void *module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!module) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe): других потоков ещё нет
    (void)std::fprintf(stderr, "Модуль не загрузился: %s\n", dlerror());
    return 1;
  }
  const auto entry = reinterpret_cast<Entry>(dlsym(module, "ReaperPluginEntry"));
  if (!entry) {
    (void)std::fputs("В модуле нет ReaperPluginEntry\n", stderr);
    return 1;
  }

  reaper_plugin_info_t rec{};
  rec.caller_version = REAPER_PLUGIN_VERSION;
  rec.Register = fakeRegister;
  rec.GetFunc = fakeGetFunc;

  REAPER_PLUGIN_HINSTANCE instance = module;
  if (entry(instance, &rec) != 1) {
    (void)std::fputs("Расширение не загрузилось с поддельным API\n", stderr);
    return 1;
  }

  int failures = 0;
  const auto timeout = std::chrono::seconds(5);

  // Буферы вывода копируются в копию и выводились бы дважды.
  (void)std::fflush(nullptr);
  const pid_t exiting = fork();
  if (exiting == 0)
    std::exit(0); // NOLINT(concurrency-mt-unsafe): ровно то, что делает SWELL в копии
  if (!exitsCleanly(exiting, timeout)) {
    (void)std::fputs("FAIL: копия после fork не завершилась через exit()\n", stderr);
    ++failures;
  }

  (void)std::fflush(nullptr);
  const pid_t unloading = fork();
  if (unloading == 0) {
    (void)entry(instance, nullptr);
    std::_Exit(0);
  }
  if (!exitsCleanly(unloading, timeout)) {
    (void)std::fputs("FAIL: копия после fork не завершилась после выгрузки расширения\n",
                     stderr);
    ++failures;
  }

  (void)entry(instance, nullptr);
  (void)dlclose(module);

  if (failures == 0)
    std::puts("Копии процесса с расширением завершаются");
  return failures == 0 ? 0 : 1;
}
