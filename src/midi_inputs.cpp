#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_GetNumMIDIInputs
#define REAPERAPI_WANT_GetMIDIInputName
#define REAPERAPI_WANT_GetMIDIInputNameNoAlias
#define REAPERAPI_WANT_get_config_var

#include "midi_inputs.hpp"

// SWELL объявляет max и min макросами, и они ломают стандартную библиотеку.
#define WDL_NO_DEFINE_MINMAX

#include <reaper_plugin.h>
#include <reaper_plugin_functions.h>

#include <array>
#include <cstddef>
#include <string_view>
#include <utility>

namespace training::reaper {
namespace {

/// Начало имени петлевых входов REAPER: «REAPER Loopback 1» и далее.
constexpr std::string_view kLoopbackPrefix = "REAPER Loopback";

} // namespace

std::vector<MidiInput> midiInputs() {
  std::vector<MidiInput> inputs;
  std::array<char, 256> text{};
  const int count = GetNumMIDIInputs();
  for (int device = 0; device < count; ++device) {
    // Отключённое устройство REAPER помнит по имени, но отвечает, что его нет.
    if (!GetMIDIInputName(device, text.data(), static_cast<int>(text.size())))
      continue;

    MidiInput input{.device = device, .name = text.data(), .key = {}, .enabled = false};
    text.fill('\0');
    if (GetMIDIInputNameNoAlias(device, text.data(), static_cast<int>(text.size())))
      input.key = text.data();
    if (input.key.empty())
      input.key = input.name;
    if (input.key.starts_with(kLoopbackPrefix))
      continue;

    input.enabled = midiInputEnabled(device);
    inputs.push_back(std::move(input));
  }
  return inputs;
}

bool midiInputEnabled(int device) {
  if (device == kVirtualKeyboard)
    return true;
  if (device < 0)
    return false;

  // Маска — байты подряд, бит устройства — `номер % 8` в байте `номер / 8`
  // (findings.md, R3).
  int size = 0;
  const auto *mask = static_cast<const unsigned char *>(get_config_var("midiins", &size));
  const auto byte = static_cast<std::size_t>(device / 8);
  if (!mask || size <= 0 || byte >= static_cast<std::size_t>(size))
    return false;
  return (mask[byte] & (1U << static_cast<unsigned>(device % 8))) != 0;
}

std::optional<MidiInput> findMidiInput(const std::vector<MidiInput> &inputs,
                                       const std::string &key, int device) {
  std::optional<MidiInput> found;
  for (const MidiInput &input : inputs) {
    if (input.key != key)
      continue;
    if (input.device == device)
      return input;
    if (!found)
      found = input;
  }
  return found;
}

} // namespace training::reaper
