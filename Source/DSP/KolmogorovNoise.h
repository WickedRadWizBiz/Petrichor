#pragma once

#include "DspCore.h"

namespace petrichor
{

/**
    Turbulence modulation source with a von Karman / Kolmogorov spectrum.

    White Gaussian noise is shaped by a "-5/3 fractional pole" filter: a cascade of interleaved
    pole/zero pairs whose average slope is -50/3 dB per decade in power (|H|^2 ~ f^-5/3), i.e.
    Kolmogorov's inertial range E(k) = C eps^(2/3) k^(-5/3). Below the corner frequency the
    spectrum is flat (energy-containing eddies) and a final pole adds the dissipation range.

    The filter is designed once at a fixed resolution of kStepsPerCorner internal samples per
    second-of-corner-frequency. The corner is then moved by time-warping: the internal clock
    advances by kStepsPerCorner * fc * dt per call and the output is Hermite-interpolated. This
    makes the spectrum self-similar under the Strouhal scaling f = St U / L - when the wind
    speeds up, the whole turbulence spectrum slides up in frequency with its shape (and its unit
    variance) intact.
*/
class KolmogorovNoise
{
public:
    static constexpr int kStepsPerCorner = 128;
    static constexpr int kSections       = 4;

    void reset (uint32_t seed) noexcept;

    /** Advances the process by dt seconds with the spectral corner at cornerHz.
        Returns a zero-mean, unit-variance sample. */
    float advance (float cornerHz, float dt) noexcept;

    float current() const noexcept { return output; }

    /** Power spectral slope the cascade approximates in its inertial range. */
    static constexpr float kInertialSlope = -5.0f / 3.0f;

private:
    struct Section { float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f; };
    struct Design  { Section sections[kSections]; float inputGain = 1.0f; };

    static const Design& design() noexcept;
    float step() noexcept;

    Rng rng;
    float state[kSections] {};
    float history[4] {};
    float phase = 0.0f;
    float output = 0.0f;
};

} // namespace petrichor
