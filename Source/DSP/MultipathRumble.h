#pragma once

#include <vector>
#include "KolmogorovNoise.h"
#include "StormWind.h"

namespace petrichor
{

/**
    Thunder's rumble as multipath propagation: R(t) = integral S(t - tau) h(tau) d tau.

    h(tau) is realised per distance zone as a cluster of delayed, individually low-passed taps
    (longer paths are absorbed more) whose delays and gains drift with slow turbulence - so the
    reflections "roll" - feeding an 8-line feedback delay network for the diffuse tail.

    Three zones span near -> far. Each voice sends to them according to its strike distance:
    hard (close) strikes excite a short, bright, localised resonance; soft (distant) strikes a
    long, dark, heavily diffused roll. Wet level also rises with distance.
*/
class MultipathRumble
{
public:
    static constexpr int kZones = 3;
    static constexpr int kTaps  = 12;
    static constexpr int kLines = 8;

    void prepare (double sampleRate, uint32_t seed);

    /** RT60 of the farthest zone; nearer zones scale from it. */
    void setDecay (float farRt60Seconds) noexcept;

    /** Rolling modulation, called at control rate. */
    void controlTick (const WindState& wind, float dt) noexcept;

    /** Adds the wet stereo output. zoneInputs[z] holds numSamples mono samples per zone. */
    void process (const float* const* zoneInputs, float* left, float* right, int numSamples) noexcept;

    /** Equal-power send weights for a strike at distance fraction d (0 = closest, 1 = farthest). */
    static void zoneWeights (float distanceFraction, float (&weights)[kZones]) noexcept;

    void clear() noexcept;

private:
    struct DelayLine
    {
        std::vector<float> buffer;
        int mask = 0, write = 0;

        void allocate (int minSize)
        {
            int size = 1;
            while (size < minSize) size <<= 1;
            buffer.assign ((size_t) size, 0.0f);
            mask = size - 1;
            write = 0;
        }
        void push (float x) noexcept { buffer[(size_t) write] = x; write = (write + 1) & mask; }
        float read (int delay) const noexcept { return buffer[(size_t) ((write - delay) & mask)]; }
        float readFractional (float delay) const noexcept
        {
            const int i = (int) delay;
            const float f = delay - (float) i;
            const float a = read (i), b = read (i + 1);
            return a + f * (b - a);
        }
    };

    struct Tap
    {
        float baseDelay = 0.0f, delay = 0.0f, gain = 0.0f, rollGain = 1.0f;
        float gainL = 0.0f, gainR = 0.0f;
        OnePoleLP tone;
        KolmogorovNoise roll;
    };

    struct Zone
    {
        DelayLine input;
        Tap taps[kTaps];
        float rollDepthSamples = 0.0f;
        float rollCornerHz = 0.2f;

        DelayLine lines[kLines];
        int lengths[kLines] {};
        float feedback[kLines] {};
        OnePoleLP damping[kLines];
        float rt60 = 1.0f, toneHz = 4000.0f;
    };

    void setZoneRt60 (Zone& zone, float rt60) noexcept;

    float fs = 48000.0f;
    Zone zones[kZones];
    float farRt60 = -1.0f;
};

} // namespace petrichor
