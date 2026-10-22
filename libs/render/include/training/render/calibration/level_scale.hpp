#pragma once

#include "training/render/common/dashes.hpp"
#include "training/render/common/fill.hpp"
#include "training/render/common/label.hpp"
#include "training/render/common/outline.hpp"
#include "training/render/element.hpp"

#include <optional>
#include <vector>

namespace training::render {

/// Что отмечено на шкале уровней; пустое не рисуется. Уровни — dBFS.
struct ScaleMarks {
  std::optional<double> level = std::nullopt;
  std::optional<double> noise = std::nullopt;
  std::vector<double> attacks = {};
  std::optional<double> peak = std::nullopt;
  std::optional<double> oldThreshold = std::nullopt;
  std::optional<double> newThreshold = std::nullopt;
};

/// Шкала уровней −100…0 dBFS панели калибровки: дорожка с текущим уровнем,
/// шумом, атаками, полосой атак, пиком и порогами тишины, подписи отметок и
/// деления.
class LevelScale : public Element {
public:
  /// Создаёт шкалу без отметок.
  LevelScale();

  /// Ставит отметки шкалы.
  void setMarks(ScaleMarks marks);

  /// Рисует шкалу во всё место кадра: дорожка — от 0.42 до 0.60 высоты места,
  /// отметки — от 0.34 до 0.68; подписи отметок — над и под ними, деления —
  /// у низа места. Подписи одного ряда не налезают друг на друга: подпись,
  /// которой не хватило места у отметки, сдвигается от соседней.
  void draw(const Frame &frame) const override;

private:
  ScaleMarks marks_;
  Fill track_;
  Fill noise_;
  Fill stripe_;
  Fill level_;
  Fill band_;
  Outline frame_;
  Fill attack_;
  Dashes oldThreshold_;
  Fill newThreshold_;
  Fill peak_;
  Label oldLabel_;
  Label newLabel_;
  Label noiseLabel_;
  Label pickLabel_;
  Label peakLabel_;
  std::vector<Label> divisions_;
};

} // namespace training::render
