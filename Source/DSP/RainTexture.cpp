#include "RainTexture.h"

namespace petrichor
{

namespace
{
    /** sin(2 pi x) for x in [0, 1), parabolic approximation with one refinement step. */
    inline float fastSinCycles (float x) noexcept
    {
        const float t = x < 0.5f ? x : x - 1.0f;     // [-0.5, 0.5)
        const float y = 8.0f * t - 16.0f * t * std::abs (t);
        return 0.225f * (y * std::abs (y) - y) + y;
    }

    /** Centre frequency of an impact's noise burst for a given impact speed. */
    inline float impactCentreHz (float speed) noexcept
    {
        // ~1.2 kHz for drizzle at 2 m/s, ~2.9 kHz for a 2 mm drop, ~5.6 kHz for wind-driven rain.
        return 700.0f * std::pow (std::max (speed, 0.3f), 0.75f);
    }
}

void RainTexture::prepare (double sampleRate, uint32_t seed)
{
    fs = (float) sampleRate;
    rng.setSeed (seed);
    numGrains = 0;
    samplesToNextDrop = 0.0f;
    hissL.reset();
    hissR.reset();
    hissHpL.setCutoff (150.0f, fs);
    hissHpR.setCutoff (150.0f, fs);
    hissGain.reset (0.0f);
    smoothedRate = rainRate = 0.0f;
}

void RainTexture::controlTick (const WindState& wind, const RainSettings& s, float dt) noexcept
{
    windSpeed = wind.speed;
    level = clampf (s.level, 0.0f, 1.0f);
    surface = clampf (s.surface, 0.0f, 1.0f);

    // R tracks the absolute amplitude of the wind LFO.
    const float exponent = 3.0f * clampf (s.coupling, 0.0f, 1.0f);
    const float target = std::max (s.baseRateMMh, 0.0f) * std::pow (std::max (wind.gustFactor, 0.0f), exponent);
    smoothedRate += (target - smoothedRate) * (1.0f - std::exp (-dt / 0.25f));
    rainRate = smoothedRate;

    const float previousGrainRate = grainRate;

    if (rainRate < 0.01f || level <= 0.0f)
    {
        grainRate = 0.0f;
        meanDiameter = 0.0f;
        meanImpactSpeed = 0.0f;
    }
    else
    {
        lambda = atmos::marshallPalmerLambda (rainRate);
        grainRate = atmos::dropNumberFlux (rainRate) * kCollectorArea;

        // Mean of the truncated exponential, nudged up for flux weighting.
        meanDiameter = atmos::kMinDropMM + 1.25f / lambda;
        meanImpactSpeed = atmos::impactSpeed (atmos::terminalVelocity (meanDiameter), windSpeed);
    }

    // Poisson arrivals are memoryless: rescale the pending wait when the rate changes.
    if (grainRate > 0.0f && previousGrainRate > 0.0f)
        samplesToNextDrop *= previousGrainRate / grainRate;
    else if (grainRate > 0.0f)
        samplesToNextDrop = -std::log (rng.uniformOpen()) * fs / grainRate;

    // Hiss: wider and brighter as drops and wind speed up.
    const float hissCutoff = 0.45f * impactCentreHz (std::max (meanImpactSpeed, 0.5f));
    hissL.set (hissCutoff, 0.6f, fs);
    hissR.set (hissCutoff * 1.03f, 0.6f, fs);

    const float hissTarget = grainRate > 0.0f
                           ? 0.010f * std::sqrt (grainRate / 200.0f) * std::sqrt (meanImpactSpeed / 4.0f)
                           : 0.0f;
    hissGain.setTarget (hissTarget, std::max (1, (int) (dt * fs)));
}

void RainTexture::spawnGrain() noexcept
{
    if (numGrains >= kMaxGrains)
        return;

    const float d = atmos::sampleImpactDiameter (lambda, rng);
    const float vt = atmos::terminalVelocity (d);
    const float speed = atmos::impactSpeed (vt, windSpeed);
    const float slant = atmos::slantSine (vt, windSpeed);

    auto& g = grains[numGrains++];

    // Impact: kinetic energy ~ D^3 v^2, so amplitude ~ D^1.5 v (compressed for audibility).
    const float energyRef = std::pow (d / 2.0f, 1.5f) * (speed / 6.5f);
    const float amp = 0.5f * std::pow (energyRef, 0.7f) * (0.7f + 0.6f * rng.uniform());

    const float tau = 0.0004f + 0.0016f * d;
    g.noiseEnv = amp;
    g.noiseDecay = std::exp (-1.0f / (tau * fs));
    g.resonator.reset();
    g.resonator.set (clampf (impactCentreHz (speed) * (0.8f + 0.4f * rng.uniform()), 200.0f, 0.42f * fs), 1.3f, fs);

    // Rain slants downwind: bias the image toward the side the wind blows to.
    const float pan = clampf (rng.bipolar() * (1.0f - 0.5f * slant) + 0.6f * slant, -1.0f, 1.0f);
    panGains (pan, g.gainL, g.gainR);

    float life = tau * 9.2f; // -80 dB

    // Larger drops landing in water entrain a bubble that rings at its Minnaert frequency.
    g.bubbleEnv = 0.0f;
    const float bubbleChance = surface * (d > 1.0f ? 0.45f : 0.1f);
    if (rng.uniform() < bubbleChance)
    {
        const float radius = d * (0.25f + 0.35f * rng.uniform());
        const float f = std::min (atmos::minnaertFrequency (radius), 0.4f * fs);
        const float bubbleTau = 0.008f * std::sqrt (2000.0f / f);
        g.bubblePhase = 0.0f;
        g.bubbleInc = f / fs;
        g.bubbleChirp = std::exp (std::log (1.0f + 0.15f + 0.2f * rng.uniform()) / (bubbleTau * 4.0f * fs));
        g.bubbleEnv = amp * 0.6f * surface;
        g.bubbleDecay = std::exp (-1.0f / (bubbleTau * fs));
        life = std::max (life, bubbleTau * 9.2f);
    }

    g.samplesLeft = (int) (life * fs) + 1;
}

void RainTexture::render (float* left, float* right, int numSamples) noexcept
{
    const float outGain = 0.9f * level * std::sqrt (level);

    for (int i = 0; i < numSamples; ++i)
    {
        if (grainRate > 0.0f)
        {
            samplesToNextDrop -= 1.0f;
            int spawned = 0;
            while (samplesToNextDrop <= 0.0f && spawned++ < 8)
            {
                spawnGrain();
                samplesToNextDrop += -std::log (rng.uniformOpen()) * fs / grainRate;
            }
        }

        float l = 0.0f, r = 0.0f;

        for (int k = 0; k < numGrains;)
        {
            auto& g = grains[k];

            g.resonator.process (rng.bipolar() * g.noiseEnv);
            g.noiseEnv *= g.noiseDecay;
            float y = g.resonator.bandNormalised();

            if (g.bubbleEnv > 0.0f)
            {
                y += fastSinCycles (g.bubblePhase) * g.bubbleEnv;
                g.bubblePhase += g.bubbleInc;
                if (g.bubblePhase >= 1.0f) g.bubblePhase -= 1.0f;
                g.bubbleInc = std::min (g.bubbleInc * g.bubbleChirp, 0.45f);
                g.bubbleEnv *= g.bubbleDecay;
            }

            l += y * g.gainL;
            r += y * g.gainR;

            if (--g.samplesLeft <= 0)
                grains[k] = grains[--numGrains];
            else
                ++k;
        }

        const float hg = hissGain.next();
        if (hg > 0.0f)
        {
            hissL.process (rng.bipolar());
            hissR.process (rng.bipolar());
            l += hissHpL.process (hissL.lp) * hg;
            r += hissHpR.process (hissR.lp) * hg;
        }

        left[i]  += l * outGain;
        right[i] += r * outGain;
    }
}

} // namespace petrichor
