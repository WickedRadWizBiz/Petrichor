#pragma once

#include <vector>
#include "Atmosphere.h"
#include "KolmogorovNoise.h"
#include "PianoHybrid.h"
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
    float multipath       = 0.2f;    // 0..1, rolling multipath in the hammer-string contact
    float rollDepth       = 0.5f;    // 0..1, how far the thunder's roll darkens the low strings
    float rollSeconds     = 3.5f;    // how long the thunder rolls (longer for distant strikes)
    float character       = 0.0f;    // 0 = I, measured grand (Salamander C5); 1 = II, Rhodes-style tine piano
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

    Each partial n is rendered by two complex one-pole resonators: the "prompt" mode and the slowly
    decaying, slightly detuned "aftersound" mode, giving the double decay and beating of real unison
    strings.

    PIANO I IS RESYNTHESISED FROM A REAL PIANO. For I, every partial's frequency ratio, prompt and
    aftersound amplitude, decay rates, beat and phase come from PianoHybrid (the Salamander Grand
    Piano analysed into exactly this two-mode model), interpolated by key and velocity, and the
    strike's own noise - hammer, action and soundboard, everything that is not on a partial - is
    played back as the measured attack residual. II stays synthetic. The modes stay modes, so the
    storm can still bend, darken and texture them.

    THUNDER IS THE HAMMER-STRING INTERACTION, AND A ROLL IN THE LOW STRINGS. The strike is the
    lightning impulse: MIDI velocity becomes a distance x ~ (127 - V). The force the string receives
    is F(t) = [gamma felt pulse + crack A e^(-t/tau) n(t)] * h(t), where h(t) is a short train of
    softer re-contacts, and every partial's excitation is weighted by exp(-alpha(f_n) x) with
    alpha ~ f^2 - the atmospheric absorption applied exactly, partial by partial. Then, on keys
    below middle C (more the lower the key, most in thunder's own register), the thunder rolls
    through the string: a few irregular swells, each one a gentle low-pass sweep that darkens the
    note toward thunder's register and lowers its pitch by a few cents, with a trace of level and a
    soft body rumble - colour, not volume.

    WIND (fused) bends the note the way wind bends an Aeolian tone: f ~ U, so the pitch ratio is
    (U_key / U_mean)^depth, where U_key combines the storm's gusts with a per-key Kolmogorov
    turbulence LFO whose corner follows the Strouhal law f_c = St U / L (L = the note's wavelength:
    low keys brood slowly, high keys flutter). The same local wind tilts the brightness and sweeps
    a gentle resonant band-pass across the partials.

    RAIN (fused) lands on the strings: drops are impulses into the ringing modes, shaped by the
    drop's impact spectrum, and a continuous rain hiss excites the modes so the texture sounds in
    the note's own partials. Both scale with the strike's open-loop reference energy.
*/
class PianoVoice
{
public:
    static constexpr int kMaxPartials     = 96;
    static constexpr int kModesPerPartial = 2;
    static constexpr int kMaxModes        = kMaxPartials * kModesPerPartial;
    static constexpr int kLanes           = 8;

    void prepare (double sampleRate, uint32_t seed);

    /** Strike (or re-strike) the string. velocity01 = MIDI velocity / 127. */
    void strike (int midiKey, float velocity01, const StrikeSettings& settings);

    /** Key released with no pedal: lower the damper (keys above F6 have none). */
    void startDamper() noexcept;

    /** Silences the voice at once (all-sound-off). Real-time safe. */
    void kill() noexcept;

    /** Fade out quickly so the voice can be reused (voice stealing). */
    void beginSteal() noexcept;

    /** Called every control period. rainHiss is the fused rain-hiss level relative to the string. */
    void controlTick (const WindState& wind, const VoiceWindSettings& settings, float rainHiss, float dt) noexcept;

    /** A raindrop lands on this string. amount is relative to the string's current amplitude. */
    void rainDrop (float amount, float impactCentreHz) noexcept;

    /** Renders numSamples of mono output, overwriting out. */
    void render (float* out, int numSamples) noexcept;

    /** Renders mono plus a stereo "side" signal (added left, subtracted right). */
    void render (float* out, float* side, int numSamples) noexcept;

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

    /** Current thunder roll (0..1) and the low-pass corner it holds the note at (0 = open). */
    float getThunderRoll() const noexcept     { return thunderNow; }
    float getThunderCutoffHz() const noexcept { return lastThunderHz; }

    int getNumActiveModes() const noexcept { return numModes; }
    int getForceLength() const noexcept    { return forceLength; }
    const float* getForce() const noexcept { return force.data(); }

    // Exposed for tests / visualisation.
    static float inharmonicity (int midiKey) noexcept;
    static float stretchCents (int midiKey) noexcept;
    static float promptT60 (int midiKey) noexcept;

    /** How strongly the thunder rolls through a key: 0 from middle C up, rising to 1 at A0. */
    static float thunderKeyWeight (int midiKey) noexcept;

private:
    float tickModes() noexcept;
    float tickModesDriven (float force, float crack) noexcept;
    float tickThump() noexcept;
    float tickResidual() noexcept;
    void  buildForce (const StrikeSettings& s, float kNorm, float corner, float order, float impulse, float strikePoint);
    void  applyPitchRatio (float ratio) noexcept;
    void  updateWeights (float centreHz, float emphasis, float tilt, float thunderHz) noexcept;
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
    alignas (32) float driveIm[kMaxModes] {};   // imaginary part: the aftersound's measured phase
    alignas (32) float partShape[kMaxModes] {}; // how a drop on the string reaches each mode
    alignas (32) float crackDrive[kMaxModes] {}; // how the crack's broadband force reaches each mode
    alignas (32) float hissDrive[kMaxModes] {};
    alignas (32) float weight[kMaxModes] {};
    alignas (32) float modeHz[kMaxModes] {};
    alignas (32) float octaves[kMaxModes] {}; // log2(f_mode / f1), for the brightness tilt
    alignas (32) float hitShape[kMaxModes] {}; // scratch for rain drops
    int numModes = 0;

    // Hammer-string contact: the full excitation force, built at strike time (allocated in prepare).
    std::vector<float> force, scratch;
    std::vector<float> crackForce, crackScratch; // the crack, kept apart: it drives the modes by their shape
    int maxExcitation = 0, maxForce = 0;
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
    float lastCentre = -1.0f, lastEmphasis = -1.0f, lastTilt = 0.0f, lastThunderHz = 0.0f;
    float hissAmp = 0.0f;
    float character = 0.0f; // I (0) .. II (1) of the latest strike
    float lastSide = 0.0f;
    float attackGain = 1.0f, attackCoef = 1.0f; // II's soft onset
    // Open-loop reference envelope of the strike (captured after contact, decays at the string's
    // slowest rate). Rain rides on this, never on the live energy, so it cannot sustain itself.
    float referenceEnergy = 0.0f, referenceDecay = 0.0f;
    float strikeEnergy = 0.0f; // expected energy of the latest strike, until the contact has delivered it

    // Wind
    KolmogorovNoise lfo;
    float lfoValue = 0.0f;

    // Piano I's measured partials for the latest strike (scratch, kept off the stack).
    PianoHybrid::Partials measured;

    // The measured attack residual (hammer, action and soundboard noise), re-pitched to the key.
    struct Residual
    {
        bool  active = false;
        const std::uint8_t* data[2] { nullptr, nullptr };
        float gain[2] {};
        float pos = 0.0f, inc = 1.0f;
        int   length = 0, delay = 0;
        OnePoleLP lp1, lp2; // distance darkens it like the strike
    } residual;

    // Thunder rolling through the low strings: a few irregular swells, t_k, width tau_k, height a_k.
    struct ThunderRoll
    {
        static constexpr int kMaxSwells = 5;
        int   count = 0;
        float depth = 0.0f;
        float time[kMaxSwells] {}, width[kMaxSwells] {}, height[kMaxSwells] {};
    } roll;
    float thunderNow = 0.0f, thunderMakeup = 1.0f, windSwell = 1.0f;
    Svf   body;                       // the body's rumble under the roll
    float bodyGain = 0.0f, bodyStep = 0.0f, bodyRef = 0.0f, bodyNoiseScale = 1.0f;
    static constexpr float kBodyRumble = 0.08f; // body rumble at full roll, relative to the strike's amplitude

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
