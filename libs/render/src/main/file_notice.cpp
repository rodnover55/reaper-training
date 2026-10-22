#include "training/render/main/file_notice.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <string>
#include <string_view>
#include <vector>

namespace training::render {

FileNotice::FileNotice(std::string_view file, const std::vector<std::string> &rejected) {
  if (!rejected.empty())
    text_ = fmt::format("{}: {}", file, fmt::join(rejected, "; "));
}

void FileNotice::onWindowOpen() { opened_ = true; }

void FileNotice::onSettingsEdit() { dismiss(); }

void FileNotice::onTransportStart() { dismiss(); }

void FileNotice::dismiss() {
  if (opened_)
    dismissed_ = true;
}

std::string_view FileNotice::text() const {
  if (!opened_ || dismissed_)
    return {};
  return text_;
}

} // namespace training::render
