#pragma once

#include "field_sample.h"
#include "grid.h"

namespace wave {

// Shades one grid cell: a naturalistic base colour ramp from wave height,
// lit with fixed-direction fake Blinn-Phong lighting against the height
// field's analytical slope (gradX/gradY) so it reads as glinting water
// rather than a flat heightmap, plus thresholded (not gradient) foam and a
// fine sparkle/glitter layer. `confidence` in [0,1] desaturates toward a
// neutral steel-blue-grey as it drops, so stale data reads as the piece
// "losing certainty" rather than freezing on a possibly-wrong frame.
RGB8 colorize(const FieldSample& sample, float confidence);

} // namespace wave
