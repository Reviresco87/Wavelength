#pragma once

#include "field_sample.h"
#include "grid.h"
#include "sky_state.h"

namespace wave {

// Shades one grid cell: a naturalistic base colour ramp from wave height,
// lit with fake Blinn-Phong lighting (per the given SkyState's light
// direction and strength -- real sun position and, when available, cloud
// cover, not a fixed light) against the height field's analytical slope
// (gradX/gradY) so it reads as glinting water rather than a flat heightmap,
// plus thresholded (not gradient) foam and a fine sparkle/glitter layer.
// `confidence` in [0,1] desaturates toward a neutral steel-blue-grey as it
// drops, so stale data reads as the piece "losing certainty" rather than
// freezing on a possibly-wrong frame.
RGB8 colorize(const FieldSample& sample, float confidence, const SkyState& sky);

} // namespace wave
