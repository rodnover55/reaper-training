#pragma once

// Сообщение о не принятых строках файла настроек хоста (`timing-display`):
// сколько оно держится в строке состояния окна.

#include <string>
#include <string_view>
#include <vector>

namespace training::render {

/// Сообщение о не принятых строках файла в строке состояния окна: видно с
/// первого открытия окна до первой правки настроек в окне или до первого
/// запуска транспорта после этого открытия.
///
/// Созданный объект: сообщение ещё не видно.
///
/// Многопоточность: один поток вызовов.
class FileNotice {
public:
  /// Сообщения нет никогда.
  FileNotice() = default;

  /// @param file имя файла настроек для сообщения.
  /// @param rejected не принятые строки файла; пусто — сообщения нет
  ///   никогда.
  FileNotice(std::string_view file, const std::vector<std::string> &rejected);

  /// Отмечает открытие окна: сообщение становится видно, если его ещё не
  /// сняли.
  void onWindowOpen();

  /// Отмечает правку настроек в окне: после `onWindowOpen` снимает
  /// сообщение насовсем, до него ничего не делает.
  void onSettingsEdit();

  /// Отмечает запуск транспорта: после `onWindowOpen` снимает сообщение
  /// насовсем, до него ничего не делает.
  void onTransportStart();

  /// Сообщение, пока оно видно: имя файла, «: » и не принятые строки через
  /// «; »; иначе пусто.
  std::string_view text() const;

private:
  /// Снимает сообщение насовсем, если окно уже открывали; до открытия окна
  /// ничего не делает.
  void dismiss();

  std::string text_;
  bool opened_ = false;
  bool dismissed_ = false;
};

} // namespace training::render
