#pragma once

namespace wave {

// Classic Perlin "improved noise", ported from the public-domain reference
// implementation. 3D so (x, y, time) gives a smoothly-evolving 2D field for
// free. No platform dependencies -- compiles unchanged on the ESP32 later.
class PerlinNoise {
public:
    PerlinNoise();

    // Returns a value in approximately [-1, 1].
    float noise3(float x, float y, float z) const;

private:
    unsigned char p_[512];
};

} // namespace wave
