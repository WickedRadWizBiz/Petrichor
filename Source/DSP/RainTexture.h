#pragma once

#include "Atmosphere.h"
#include "StormWind.h"

namespace petrichor
{

struct RainSettings
{
    float baseRateMMh = 8.0f;  // R when the wind is at its mean
    float coupling    = 0.6f;  // how strongly R follows the gusts, 0..1
    float level       = 0.35f; // texture amount, 0..1
    float surface     = 0.3f;  // 0 = leaves / soil, 1 = standing water (Minnaert bubbles)
    float overlay     = 1.0f;  // gain of the parallel overlay layer (Overlay <-> Fuse blend)
    float fuse        = 0.0f;  // amount of rain fused into the piano (Overlay <-> Fuse blend)
    float follow      = 0.6f;  // how much the overlay layer follows the piano's dynamics and tone
};

/** What the rain hears of the piano (measured by the engine on the dry piano bus). */
struct PianoFollow
{
    float envelope   = 0.0f; // ~1 for moderately loud playing
    float centroidHz = 0.0f; // spectral centre of the piano sound
};

/** One drop arriving during the current block. */
struct DropEvent
{
    int   offset;    // sample offset within the block
    float amount;    // impact strength (kinetic-energy based)
    float centreHz;  // spectral centre of the impact
};

/**
    Wind-coupled Marshall-Palmer rain, as an overlay layer and as a texture fused into the piano.

    The rainfall rate follows the absolute amplitude of the storm's wind LFO,
    R(t) = R0 (1 + I G(t))^(3 c). R sets Lambda = 4.1 R^-0.21, which fixes how many drops arrive
    (flux = integral N(D) v_T(D) dD, a Poisson process) and how big they are (diameters drawn from
    the flux-weighted spectrum). A drop's impact speed |v| = sqrt(v_T(D)^2 + U^2) sets its
    brightness, so gusts (bigger drops, more wind) shift the texture upward and lulls thin it to a
    sparse, low hiss.

    OVERLAY: each drop is a grain of band-limited noise (plus a Minnaert bubble on water) layered
    beside the piano - like a texturiser it also listens to the piano: louder playing brings
    denser, louder rain, and grain spectra lean toward the piano's own spectral centre.

    FUSE: the same drops become events the engine lands on the ringing strings, a "patter"
    signal the engine uses to modulate the piano's amplitude grain by grain, and a hiss level that
    excites the strings' modes - so the rain sounds inside the notes.
*/
class RainTexture
{
public:
    static constexpr int kMaxGrains = 512;
    static constexpr int kMaxEvents = 64;

    void prepare (double sampleRate, uint32_t seed);
    void controlTick (const WindState& wind, const RainSettings& settings, const PianoFollow& piano, float dt) noexcept;

    /** Schedules the drops arriving in the next numSamples. Call before render(). */
    int beginBlock (int numSamples) noexcept;
    const DropEvent* getEvents() const noexcept { return events; }

    /** Adds the overlay layer to left/right and writes the fused patter modulator (overwrites). */
    void render (float* left, float* right, float* patterLeft, float* patterRight, int numSamples) noexcept;

    float getRainRate() const noexcept        { return rainRate; }
    float getLambda() const noexcept          { return lambda; }
    float getGrainRate() const noexcept       { return grainRate; }
    float getMeanDiameter() const noexcept    { return meanDiameter; }
    float getMeanImpactSpeed() const noexcept { return meanImpactSpeed; }
    int   getActiveGrains() const noexcept    { return numGrains; }

    /** Fused rain: hiss level that excites the strings, relative to their own amplitude. */
    float getFusedHiss() const noexcept       { return fusedHiss; }
    /** Fused rain: depth of the patter amplitude modulation. */
    float getPatterDepth() const noexcept     { return patterDepth; }
    /** Fused rain: strength of a drop landing on a string, for a drop of typical size. */
    float getFusedDropGain() const noexcept   { return fusedDropGain; }

    /** Fused rain: compressed strength of one drop, ~1 for a typical drop, at most 2.5. */
    static float fusedDropStrength (float amount) noexcept
    {
        return std::sqrt (clampf (amount / kTypicalDropAmp, 0.0f, 6.25f));
    }

    /** Effective collecting area (m^2) that turns drop flux into grains per second. */
    static constexpr float kCollectorArea = 0.015f;

    /** RMS of DropEvent::amount for typical rain, used to normalise fused drops. */
    static constexpr float kTypicalDropAmp = 0.12f;

    /** Centre frequency of an impact's noise burst for a given impact speed. */
    static float impactCentreHz (float speed) noexcept;

private:
    struct Grain
    {
        float noiseEnv, noiseDecay, audible, sign;
        Svf   resonator;
        float gainL, gainR;
        float bubblePhase, bubbleInc, bubbleChirp, bubbleEnv, bubbleDecay;
        int   samplesLeft;
    };

    struct PendingDrop
    {
        float diameter, speed, slant, amp;
    };

    void spawnGrain (const PendingDrop& drop) noexcept;

    float fs = 48000.0f;
    Rng rng;
    Grain grains[kMaxGrains] {};
    int numGrains = 0;

    DropEvent events[kMaxEvents] {};
    PendingDrop pending[kMaxEvents] {};
    int numEvents = 0;

    float samplesToNextDrop = 0.0f;
    float rainRate = 0.0f, smoothedRate = 0.0f;
    float lambda = 4.0f, grainRate = 0.0f;
    float meanDiameter = 0.0f, meanImpactSpeed = 0.0f;
    float windSpeed = 0.0f;
    float level = 0.0f, surface = 0.0f, overlay = 1.0f;
    float followFactor = 1.0f, spectralLean = 0.0f, pianoCentroid = 0.0f;
    float fusedHiss = 0.0f, patterDepth = 0.0f, fusedDropGain = 0.0f;

    // Diffuse hiss bed of the overlay layer (decorrelated left / right).
    Svf hissL, hissR;
    OnePoleHP hissHpL, hissHpR;
    Ramp hissGain;
};

} // namespace petrichor
