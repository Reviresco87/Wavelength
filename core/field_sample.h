#pragma once

namespace wave {

// Everything the shading model (palette::colorize) needs to turn one grid
// cell into a colour -- computed per-pixel by WaveEngine::sampleField.
struct FieldSample {
    float height = 0.0f;      // summed deterministic wave height, drives base colour
    float gradX = 0.0f;       // d(height)/dx, analytical from the wave components -- drives lighting
    float gradY = 0.0f;       // d(height)/dy
    float foamAmount = 0.0f;  // 0..1, precomputed likelihood of foam/whitecap at this cell
    float sparkle = 0.0f;     // 0..1, fast-evolving twinkle factor (sun-glitter), only shows on lit facets
};

} // namespace wave
