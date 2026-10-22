#include "training/core/hit_window.hpp"

namespace training::core {

Hit HitWindow::hit(Interval deviation) const {
  if ((deviation - offset_).abs() <= exactTolerance_)
    return Hit::Target;
  return deviation >= early() && deviation <= late() ? Hit::Good : Hit::Bad;
}

} // namespace training::core
