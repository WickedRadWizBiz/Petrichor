#pragma once

#include <vector>
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
    float absorption      = 0.5f;    // 0..1, scales alpha(f): how strongly distance darkens the strike
    float crackLevel      = 0.5f;    // broadband crack S(t) = A e^(-t/tau) n(t) in the hammer force
    float multipath       = 0.35f;   // 0..1, rolling multipath in the hammer-string contact
    bool  softPedal       = false;   // una corda
};

/** Wind fused into the note, read every control tick. */
struct VoiceWindSettings
{
    float fuse   = 1.0f;  // 0 = overlay only (note untouched), 1 = fully fused
    float pitch  = 0.35f; // how far the note's pitch follows the wind, like an Aeolian tone (f ~ U)
    float timbre = 0.4f;  // gust-driven brightness tilt + resonant band-pass sweep
};

/**
    One piano key, synthesised modally.

    Each partial n of the stiff string sits at f_n = n f0 sqrt(1 + B n^2) and is rendered by two
    complex one-pole resonators (the in-phase "prompt" mode and the slowly decaying, slightly
    detuned "aftersound" mode), giving the double decay and beating of real unison strings.

    THUNDER IS THE HAMMER-STRING INTERACTION. The strike is the lightning impulse: MIDI velocity
    becomes a distance x ~ (127 - V). The force the string receives is
        F(t) = [gamma felt pulse + crack A e^(-t/tau) n(t)] * h(t)
    where h(t) is a multipath train of delayed, progressively darker re-contacts spaced by the
    string's own reflection time (strike point to termination and back). Close strikes are a single
    sharp contact; distant ones roll on through several smeared contacts. Every partial's
    excitation is weighted by exp(-alpha(f_n) x) with alpha ~ f^2 - the atmospheric absorption
    applied exactly, partial by partial - and distant strikes also leave a louder, longer
    aftersound (decay and "wet" share rise as velocity falls).

    WIND (fused) bends the note the way wind bends an Aeolian tone: f ~ U, so the pitch ratio is
    (U_key / U_mean)^depth, where U_key combines the storm's gusts with a per-key Kolmogorov
    turbulence LFO whose corner follows the Strouhal law f_c = St U / L (L = the note's wavelength:
    low keys brood slowly, high keys flutter). The same local wind tilts the brightness and sweeps
    a gentle resonant band-pass across the partials.

    RAIN (fused) lands on the strings: drops are impulses into the ringing modes, shaped by the
    drop's impact spectrum, and a continuous rain hiss excites the modes so the texture sounds in
    the note's own partials. Both scale with the string's current energy.
*/
class PianoVoice
{
public:
    static constexpr int kMaxPartials     = 96;
    static constexpr int kModesPerPartial = 2;
    static constexpr int kMaxModes        = kMaxPartials * kModesPerPartial;
    static constexpr int kLanes           = 8;
    static constexpr int kMaxForce        = 16384;

    void prepare (double sampleRate, uint32_t seed);

    /** Strike (or re-strike) the string. velocity01 = MIDI velocity / 127. */
    void strike (int midiKey, float velocity01, const StrikeSettings& settings);

    /** Key released with no pedal: lower the damper (keys above F6 have none). */
    void startDamper() noexcept;

    /** Fade out quickly so the voice can be reused (voice stealing). */
    void beginSteal() noexcept;

    /** Called every control period. rainHiss is the fused rain-hiss level relative to the string. */
    void controlTick (const WindState& wind, const VoiceWindSettings& settings, float rainHiss, float dt) noexcept;

    /** A raindrop lands on this string. amount is relative to the string's current amplitude. */
    void rainDrop (float amount, float impactCentreHz) noexcept;

    /** Renders numSamples of mono output, overwriting out. */
    void render (float* out, int numSamples) noexcept;

    bool  isActive() const noexcept            { return active; }
    bool  isStealing() const noexcept          { return stealing; }
    bool  isDamped() const noexcept            { return damping; }
    int   getKey() const noexcept              { return key; }
    float getDistanceFraction() const noexcept { return distanceFraction; }
    float getStrikeDistance() const noexcept   { return strikeDistance; }
    float getVelocity() const noexcept         { return velocity; }
    float getFundamentalHz() const noexcept    { return fundamentalHz; }

    /** Current kinetic-energy estimate of the string (sum of |mode|^2), used for voice stealing. */
    float getEnergy() const noexcept { return energy; }

    /** Smoothed output level for the UI. */
    float getLevel() const noexcept { return level; }

    /** Current local turbulence (unit variance) and wind pitch ratio, for tests and the visualiser. */
    float getWindLfo() const noexcept    { return lfoValue; }
    float getPitchRatio() const noexcept { return currentRatio; }

    int getNumActiveModes() const noexcept { return numModes; }
    int getForceLength() const noexcept    { return forceLength; }
    const float* getForce() const noexcept { return force.data(); }

    // Exposed for tests / visualisation.
    static float inharmonicity (int midiKey) noexcept;
    static float stretchCents (int midiKey) noexcept;
    static float promptT60 (int midiKey) noexcept;

private:
    float tickModes() noexcept;
    float tickModesDriven (float force) noexcept;
    float tickThump() noexcept;
    void  buildForce (const StrikeSettings& s, float kNorm, float corner, float order, float impulse, float strikePoint);
    void  applyPitchRatio (float ratio) noexcept;
    void  updateWeights (float centreHz, float emphasis, float tilt) noexcept;
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
    alignas (32) float hissDrive[kMaxModes] {};
    alignas (32) float weight[kMaxModes] {};
    alignas (32) float modeHz[kMaxModes] {};
    alignas (32) float octaves[kMaxModes] {}; // log2(f_mode / f1), for the brightness tilt
    alignas (32) float hitShape[kMaxModes] {}; // scratch for rain drops
    int numModes = 0;

    // Hammer-string contact: the full excitation force, built at strike time (allocated in prepare).
    std::vector<float> force, scratch;
    int forceLength = 0, forcePos = 0;

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
    float lastCentre = -1.0f, lastEmphasis = -1.0f, lastTilt = 0.0f;
    float hissAmp = 0.0f;
    // Open-loop reference envelope of the strike (captured after contact, decays at the string's
    // slowest rate). Rain rides on this, never on the live energy, so it cannot sustain itself.
    float referenceEnergy = 0.0f, referenceDecay = 0.0f;

    // Wind
    KolmogorovNoise lfo;
    float lfoValue = 0.0f;

    // Soundboard knock that accompanies the strike (the only part heard directly).
    struct Thump
    {
        bool  active = false;
        float re = 0.0f, im = 0.0f, zr = 0.0f, zi = 0.0f;
        int   samplesLeft = 0, delay = 0;
    } thump;

    Rng rng;
};

} // namespace petrichor
