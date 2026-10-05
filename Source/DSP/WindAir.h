#pragma once

#include "Atmosphere.h"
#include "StormWind.h"

namespace petrichor
{

/**
    The audible wind itself: a low roar whose level follows the dynamic pressure (~U^2) and
    Aeolian whistles from three wires, each pitched at f = St U(t) / D_wire so the tones glide as
    the gusts ebb and flow.
*/
class WindAir
{
public:
    static constexpr int kWires = 3;

    void prepare (double sampleRate, uint32_t seed)
    {
        fs = (float) sampleRate;
        noiseScale = std::sqrt (fs / 48000.0f); // keep the roar's level independent of fs
        rng.setSeed (seed);
        roarL.reset(); roarR.reset();
        for (auto& w : whistles) w.reset();
        roarGain.reset (0.0f);
        whistleGain.reset (0.0f);
    }

    void controlTick (const WindState& wind, float level01, float dt) noexcept
    {
        const float u = wind.speed;
        const int ramp = std::max (1, (int) (dt * fs));
        const float level = clampf (level01, 0.0f, 1.0f);

        roarL.set (90.0f + 22.0f * u, 0.55f, fs);
        roarR.set (95.0f + 23.0f * u, 0.55f, fs);
        const float pressure = std::min (u / 20.0f, 1.6f);
        roarGain.setTarget (0.35f * level * pressure * pressure, ramp);

        // Vortex shedding only locks into a clean tone above a few m/s.
        const float lockIn = clampf ((u - 3.0f) / 7.0f, 0.0f, 1.0f);
        for (int i = 0; i < kWires; ++i)
        {
            const float f = clampf (atmos::strouhalFrequency (u, kWireDiameters[i]), 40.0f, 0.4f * fs);
            whistles[i].set (f, kWhistleQ, fs);
            whistleNorm[i] = std::sqrt (kWhistleQ * fs / (kPi * f)); // unit-variance narrowband output
        }
        whistleGain.setTarget (0.025f * level * lockIn * pressure, ramp);
    }

    void render (float* left, float* right, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float rg = roarGain.next();
            const float wg = whistleGain.next();
            if (rg <= 0.0f && wg <= 0.0f)
                continue;

            roarL.process (rng.bipolar() * noiseScale);
            roarR.process (rng.bipolar() * noiseScale);
            float l = roarL.lp * rg;
            float r = roarR.lp * rg;

            for (int w = 0; w < kWires; ++w)
            {
                whistles[w].process (rng.bipolar());
                const float y = whistles[w].bandNormalised() * whistleNorm[w] * wg;
                l += y * kWirePan[w];
                r += y * (1.0f - kWirePan[w]);
            }

            left[i]  += l;
            right[i] += r;
        }
    }

private:
    static constexpr float kWireDiameters[kWires] = { 2.5e-3f, 4.0e-3f, 6.5e-3f };
    static constexpr float kWirePan[kWires]       = { 0.75f, 0.35f, 0.55f };
    static constexpr float kWhistleQ = 28.0f;

    float fs = 48000.0f, noiseScale = 1.0f;
    Rng rng;
    Svf roarL, roarR;
    Svf whistles[kWires];
    float whistleNorm[kWires] { 1.0f, 1.0f, 1.0f };
    Ramp roarGain, whistleGain;
};

} // namespace petrichor
