#include <doctest/doctest.h>

#include "training/render/main/file_notice.hpp"

#include <string>
#include <vector>

using training::render::FileNotice;

namespace {

/// Сообщение файла `reaper-extstate.ini` о строках `rejected`.
FileNotice noticeOf(const std::vector<std::string> &rejected) {
  return {"reaper-extstate.ini", rejected};
}

} // namespace

TEST_CASE("Сообщение о файле: видно с открытия окна") {
  FileNotice notice = noticeOf({"mode=5 is not a grid mode — Quarters in use",
                                "window_scale_ms=500 is out of range 10…200 — 50 in use"});
  CHECK(notice.text().empty());
  notice.onWindowOpen();
  CHECK(notice.text() == "reaper-extstate.ini: mode=5 is not a grid mode — Quarters in use; "
                         "window_scale_ms=500 is out of range 10…200 — 50 in use");
}

TEST_CASE("Сообщение о файле: до открытия окна запуск транспорта его не снимает") {
  FileNotice notice = noticeOf({"mode=5 is not a grid mode — Quarters in use"});
  notice.onTransportStart();
  notice.onSettingsEdit();
  notice.onWindowOpen();
  CHECK(notice.text() == "reaper-extstate.ini: mode=5 is not a grid mode — Quarters in use");
}

TEST_CASE("Сообщение о файле: после открытия снимают запуск транспорта и правка") {
  FileNotice started = noticeOf({"mode=5 is not a grid mode — Quarters in use"});
  started.onWindowOpen();
  started.onTransportStart();
  CHECK(started.text().empty());
  // Снятое сообщение не возвращается и при новом открытии окна.
  started.onWindowOpen();
  CHECK(started.text().empty());

  FileNotice edited = noticeOf({"mode=5 is not a grid mode — Quarters in use"});
  edited.onWindowOpen();
  edited.onSettingsEdit();
  CHECK(edited.text().empty());
}

TEST_CASE("Сообщение о файле: без не принятых строк текста нет никогда") {
  FileNotice notice = noticeOf({});
  CHECK(notice.text().empty());
  notice.onWindowOpen();
  CHECK(notice.text().empty());

  FileNotice none;
  none.onWindowOpen();
  CHECK(none.text().empty());
}
