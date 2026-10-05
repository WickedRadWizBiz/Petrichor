#pragma once

#include "Atmosphere.h"
#include "StormWind.h"

namespace petrichor
{

struct RainSettings
{
    float baseRateMMh = 8.0f;  // R when the wind is at its mean
    float coupling    = 0.6f;  // how strongly R follows the gusts, 0..1
    float level       = 0.35f; // 0..1
    float surface     = 0.3f;  // 0 = leaves / soil, 1 = standing water (Minnaert bubbles)
};

/**
    Wind-coupled granular rain.

    The rainfall rate follows the absolute amplitude of the storm's wind LFO,
    R(t) = R0 (1 + I G(t))^(3 c), so gusts bring heavier rain. R sets the Marshall-Palmer slope
    Lambda = 4.1 R^-0.21, which fixes both how many drops arrive (flux = integral N(D) v_T(D) dD,
    a Poisson process) and how big they are (diameters drawn from the flux-weighted spectrum).

    Each grain is an impact: band-limited noise whose centre frequency rises with the impact speed
    |v| = sqrt(v_T(D)^2 + U^2), so gusts (bigger drops, more wind) shift the texture upward. Big
    drops on water may also ring a Minnaert bubble. Underneath sits a diffuse hiss whose
    bandwidth follows the mean impact speed - a sparse, low hiss when the wind ebbs.
*/
class RainTexture
{
public:
    static constexpr int kMaxGrains = 512;

    void prepare (double sampleRate, uint32_t seed);
    void controlTick (const WindState& wind, const RainSettings& settings, float dt) noexcept;
    void render (float* left, float* right, int numSamples) noexcept;

    float getRainRate() const noexcept       { return rainRate; }
    float getLambda() const noexcept         { return lambda; }
    float getGrainRate() const noexcept      { return grainRate; }
    float getMeanDiameter() const noexcept   { return meanDiameter; }
    float getMeanImpactSpeed() const noexcept { return meanImpactSpeed; }
    int   getActiveGrains() const noexcept   { return numGrains; }

    /** Effective collecting area (m^2) that turns drop flux into grains per second. */
    static constexpr float kCollectorArea = 0.015f;

private:
    struct Grain
    {
        float noiseEnv, noiseDecay;
        Svf   resonator;
        float gainL, gainR;
        float bubblePhase, bubbleInc, bubbleChirp, bubbleEnv, bubbleDecay;
        int   samplesLeft;
    };

    void spawnGrain() noexcept;

    float fs = 48000.0f;
    Rng rng;
    Grain grains[kMaxGrains] {};
    int numGrains = 0;

    float samplesToNextDrop = 0.0f;
    float rainRate = 0.0f, smoothedRate = 0.0f;
    float lambda = 4.0f, grainRate = 0.0f;
    float meanDiameter = 0.0f, meanImpactSpeed = 0.0f;
    float windSpeed = 0.0f;
    float level = 0.0f, surface = 0.0f;

    // Diffuse hiss bed (decorrelated left / right).
    Svf hissL, hissR;
    OnePoleHP hissHpL, hissHpR;
    Ramp hissGain;
};

} // namespace petrichor
