#pragma once

#include "Atmosphere.h"
#include "KolmogorovNoise.h"
#include "StormWind.h"

namespace petrichor
{

/** Settings captured when a key is struck. */
struct StrikeSettings
{
    float a4Hz            = 440.0f;
    float hammerHardness  = 0.5f;    // 0..1, felt stiffness
    float decayScale      = 1.0f;    // multiplies every string T60
    float unisonCents     = 1.2f;    // mistuning between the unison strings
    float maxDistanceM    = 1200.0f; // distance of a velocity-1 strike
    float toneAbsorption  = 0.4f;    // how much of the absorption reaches the string tone (transient: always 100%)
    float crackLevel      = 0.5f;    // level of the hammer / lightning transient
    bool  softPedal       = false;   // una corda
};

/** Live wind-coupling amounts, read every control tick. */
struct VoiceWindSettings
{
    float drift  = 0.35f; // Doppler micro-pitch depth, 0..1
    float filter = 0.4f;  // resonant band-pass sweep / swell, 0..1
};

/**
    One piano key, synthesised modally.

    Each partial n of the stiff string sits at f_n = n f0 sqrt(1 + B n^2) and is rendered by two
    complex one-pole resonators (the in-phase "prompt" mode and the slowly decaying, slightly
    detuned "aftersound" mode), giving the double decay and beating of real unison strings.
    A non-linear felt hammer pulse drives all modes, so contact time - and therefore brightness -
    follows the strike velocity.

    The strike is also the lightning impulse: MIDI velocity is mapped to a distance
    x ~ (127 - V), and the attack transient S(t) = A e^(-t/tau) n(t) is passed through an
    atmospheric-absorption low-pass whose cutoff falls as sqrt(1/x). The same exp(-alpha(f) x)
    weighting can be applied directly to every partial's excitation (toneAbsorption).

    Each voice owns a Kolmogorov LFO whose corner follows the Strouhal law f = St U / L, with the
    note's wavelength as the obstacle length L. It drives a Doppler micro-pitch drift and a gentle
    resonant band-pass sweeping across the partials.
*/
class PianoVoice
{
public:
    static constexpr int kMaxPartials     = 96;
    static constexpr int kModesPerPartial = 2;
    static constexpr int kMaxModes        = kMaxPartials * kModesPerPartial;
    static constexpr int kLanes           = 8;
    static constexpr int kMaxPulse        = 4096;

    void prepare (double sampleRate, uint32_t seed);

    /** Strike (or re-strike) the string. velocity01 = MIDI velocity / 127. */
    void strike (int midiKey, float velocity01, const StrikeSettings& settings);

    /** Key released with no pedal: lower the damper (keys above F6 have none). */
    void startDamper() noexcept;

    /** Fade out quickly so the voice can be reused (voice stealing). */
    void beginSteal() noexcept;

    /** Called every control period. */
    void controlTick (const WindState& wind, const VoiceWindSettings& settings, float dt) noexcept;

    /** Renders numSamples of mono output, overwriting out. */
    void render (float* out, int numSamples) noexcept;

    bool  isActive() const noexcept            { return active; }
    bool  isStealing() const noexcept          { return stealing; }
    bool  isDamped() const noexcept            { return damping; }
    int   getKey() const noexcept              { return key; }
    float getDistanceFraction() const noexcept { return distanceFraction; }
    float getStrikeDistance() const noexcept   { return strikeDistance; }
    float getVelocity() const noexcept         { return velocity; }

    /** Current kinetic-energy estimate of the string (sum of |mode|^2), used for voice stealing. */
    float getEnergy() const noexcept { return energy; }

    /** Smoothed output level for the UI. */
    float getLevel() const noexcept { return level; }

    /** Current wind modulation, for the visualiser. */
    float getWindLfo() const noexcept { return lfoValue; }

    int getNumActiveModes() const noexcept { return numModes; }

    // Exposed for tests / visualisation.
    static float inharmonicity (int midiKey) noexcept;
    static float stretchCents (int midiKey) noexcept;
    static float promptT60 (int midiKey) noexcept;

private:
    float tickModes() noexcept;
    float tickModesDriven (float force) noexcept;
    float tickTransient() noexcept;
    void  applyPitchRatio (float ratio) noexcept;
    void  updateFilterWeights (float centreHz, float emphasis) noexcept;
    void  trimSilentModes() noexcept;
    void  foldEnvelopeIntoState() noexcept;

    float fs = 48000.0f;

    // Mode bank (structure of arrays so the inner loop vectorises).
    alignas (32) float re[kMaxModes] {};
    alignas (32) float im[kMaxModes] {};
    alignas (32) float zr[kMaxModes] {};
    alignas (32) float zi[kMaxModes] {};
    alignas (32) float zr0[kMaxModes] {};
    alignas (32) float zi0[kMaxModes] {};
    alignas (32) float omega[kMaxModes] {};
    alignas (32) float drive[kMaxModes] {};
    alignas (32) float weight[kMaxModes] {};
    alignas (32) float modeHz[kMaxModes] {};
    int numModes = 0;

    // Hammer
    float pulse[kMaxPulse] {};
    int pulseLength = 0, pulsePos = 0;

    // State
    bool active = false, damping = false, stealing = false;
    int key = 60;
    float velocity = 0.0f;
    float fundamentalHz = 261.6f;
    float env = 1.0f, dampCoef = 1.0f;
    float stealGain = 1.0f, stealStep = 0.0f;
    float energy = 0.0f, level = 0.0f;
    float outputGain = 1.0f;
    float distanceFraction = 0.0f, strikeDistance = 0.0f;
    float timeSinceStrike = 0.0f;
    float glideDepth = 0.0f;
    float currentRatio = 1.0f;
    float swellGain = 1.0f, swellTarget = 1.0f, swellStep = 0.0f;
    float lastCentre = -1.0f, lastEmphasis = -1.0f;

    // Wind
    KolmogorovNoise lfo;
    float lfoValue = 0.0f;

    // Attack transient (hammer knock = lightning crack) through the absorption cascade.
    struct Transient
    {
        bool  active = false;
        float noiseAmp = 0.0f, noiseDecay = 0.0f;
        float thumpRe = 0.0f, thumpIm = 0.0f, thumpZr = 0.0f, thumpZi = 0.0f;
        int   samplesLeft = 0, delay = 0;
        OnePoleHP highPass;
        OnePoleLP absorb[4];
    } transient;

    Rng rng;
};

} // namespace petrichor
