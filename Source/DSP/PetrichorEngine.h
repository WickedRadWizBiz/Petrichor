#pragma once

#include <array>
#include <vector>
#include "MultipathRumble.h"
#include "PianoVoice.h"
#include "RainTexture.h"
#include "StormWind.h"
#include "SympatheticResonance.h"
#include "WindAir.h"

namespace petrichor
{

/** Every user-facing control, in physical units. Mirrors the plug-in's parameter layout. */
struct EngineParams
{
    // Piano
    float hammerHardness = 0.5f;   // 0..1
    float sustain        = 1.0f;   // string decay multiplier, 0.3..3
    float unisonCents    = 1.2f;   // 0..4
    float stereoWidth    = 0.7f;   // 0..1
    float pianoLevelDb   = 0.0f;

    // Thunder = the hammer-string interaction (velocity = distance). Always fused.
    float stormDistanceM = 1200.0f; // distance of a velocity-1 strike
    float crackLevel     = 0.5f;    // 0..1, broadband crack in the hammer force
    float airAbsorption  = 0.5f;    // 0..1, scales alpha(f) applied to the strike
    float rumbleMix      = 0.35f;   // 0..1, multipath contact + body rumble
    float rumbleDecayS   = 5.0f;    // RT60 of the farthest body-rumble zone

    // Wind (Kolmogorov / Strouhal)
    float windSpeedMs    = 8.0f;    // 0..30
    float turbulence     = 0.35f;   // 0..1
    float gustLengthM    = 12.0f;   // integral length scale
    float windBlend      = 0.7f;    // 0 = overlay (audible wind layer), 1 = fused into the notes
    float windPitch      = 0.35f;   // fused: how far notes bend with the wind, 0..1
    float windTimbre     = 0.4f;    // fused: gust brightness + band-pass sweep, 0..1
    float windAir        = 0.3f;    // overlay: level of the audible wind layer, 0..1

    // Rain (Marshall-Palmer granular)
    float rainRateMMh    = 8.0f;    // 0..150
    float rainCoupling   = 0.6f;    // 0..1
    float rainLevel      = 0.35f;   // 0..1
    float rainSurface    = 0.3f;    // 0..1
    float rainBlend      = 0.5f;    // 0 = overlay layer, 1 = fused into the piano sound
    float rainFollow     = 0.6f;    // overlay: how much the rain follows the piano's dynamics

    // Global
    float tuningA4Hz     = 440.0f;
    float character      = 0.0f;    // I (acoustic grand) .. II (Rhodes-style tine piano)
    float resonance      = 0.5f;    // sympathetic string resonance (I side), 0..1
    float masterDb       = -2.0f;
};

/** Live state for the visualiser. */
struct EngineTelemetry
{
    float windSpeed = 0.0f, windGust = 0.0f, gustFactor = 1.0f;
    float rainRate = 0.0f, lambda = 0.0f, grainRate = 0.0f, meanDropMM = 0.0f, impactSpeed = 0.0f;
    int   activeVoices = 0;
    uint32_t strikeCount = 0;
    float lastStrikeDistance = 0.0f, lastStrikeKey = 0.0f, lastStrikeVelocity = 0.0f;
    std::array<float, 88> keyLevels {};
};

/**
    Petrichor Piano: a modal piano whose strikes are lightning, whose sustain breathes with
    Kolmogorov wind and which sits in a wind-coupled Marshall-Palmer rain.

    Real-time safe after prepare(): no allocation, no locks. Events are applied at block
    boundaries, so split blocks at MIDI timestamps for sample accuracy.
*/
class PetrichorEngine
{
public:
    static constexpr int kMaxVoices    = 64;
    static constexpr int kControlBlock = 32;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void setParams (const EngineParams& p) noexcept { params = p; }
    const EngineParams& getParams() const noexcept  { return params; }

    void noteOn (int midiKey, int midiVelocity) noexcept;
    void noteOff (int midiKey) noexcept;
    void setSustainPedal (bool down) noexcept;
    void setSoftPedal (bool down) noexcept { softPedal = down; }
    void allNotesOff() noexcept;
    void allSoundOff() noexcept;

    /** Renders numSamples into left/right (overwrites). */
    void process (float* left, float* right, int numSamples) noexcept;

    const EngineTelemetry& getTelemetry() const noexcept { return telemetry; }
    double getSampleRate() const noexcept { return sampleRate; }

private:
    struct VoiceSlot
    {
        PianoVoice voice;
        bool keyHeld = false;
        bool hasPending = false;
        int pendingKey = 0;
        float pendingVelocity = 0.0f;
        uint64_t startOrder = 0;
    };

    void controlTick (float dt) noexcept;
    void landDrops() noexcept;
    void renderBlock (float* left, float* right, int numSamples) noexcept;
    StrikeSettings makeStrikeSettings() const noexcept;
    int findVoiceForKey (int key) const noexcept;
    int findFreeOrStealVoice() noexcept;
    float keyPan (int key) const noexcept;

    double sampleRate = 48000.0;
    EngineParams params;

    std::vector<VoiceSlot> voices = std::vector<VoiceSlot> (kMaxVoices); // heap: each voice carries its contact buffers
    StormWind wind;
    RainTexture rain;
    WindAir air;
    MultipathRumble rumble;

    SympatheticResonance sympathetic;
    std::vector<float> voiceBuffer, sideBuffer, pianoMono, pianoLeft, pianoRight, patterLeft, patterRight;
    std::array<std::vector<float>, MultipathRumble::kZones> sendBuffers;

    // What the rain hears of the piano, measured on the dry piano bus.
    PianoFollow pianoFollow;
    double followEnergy = 0.0, followSlopeEnergy = 0.0;
    int followSamples = 0;
    float followPrevious = 0.0f;
    Rng dropRng { 0xD20Bu };
    int numDropEvents = 0;

    int samplesUntilControl = 0;
    bool sustainPedal = false, softPedal = false;
    uint64_t noteCounter = 0;
    float voiceGainSmoothed = 1.0f, masterGainSmoothed = 0.7f;

    EngineTelemetry telemetry;
};

} // namespace petrichor
