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
}

float RainTexture::impactCentreHz (float speed) noexcept
{
    // ~1.2 kHz for drizzle at 2 m/s, ~2.9 kHz for a 2 mm drop, ~5.6 kHz for wind-driven rain.
    return 700.0f * std::pow (std::max (speed, 0.3f), 0.75f);
}

void RainTexture::prepare (double sampleRate, uint32_t seed)
{
    fs = (float) sampleRate;
    noiseScale = std::sqrt (fs / 48000.0f); // keep white-noise density (and level) independent of fs
    rng.setSeed (seed);
    numGrains = numEvents = 0;
    samplesToNextDrop = 0.0f;
    hissL.reset();
    hissR.reset();
    hissHpL.setCutoff (120.0f, fs);
    hissHpR.setCutoff (120.0f, fs);
    pinkL = pinkR = Pinker{};
    hissGain.reset (0.0f);
    smoothedRate = rainRate = grainRate = 0.0f;
}

void RainTexture::controlTick (const WindState& wind, const RainSettings& s, const PianoFollow& piano, float dt) noexcept
{
    windSpeed = wind.speed;
    level   = clampf (s.level, 0.0f, 1.0f);
    surface = clampf (s.surface, 0.0f, 1.0f);
    overlay = clampf (s.overlay, 0.0f, 1.0f);
    const float fuse = clampf (s.fuse, 0.0f, 1.0f);

    // R tracks the absolute amplitude of the wind LFO: no wind, no gust-driven swell (full at 8 m/s).
    const float exponent = 3.0f * clampf (s.coupling, 0.0f, 1.0f);
    const float drive = clampf (wind.meanSpeed / 8.0f, 0.0f, 1.0f);
    const float swell = std::max (1.0f + drive * (wind.gustFactor - 1.0f), 0.0f);
    const float target = std::max (s.baseRateMMh, 0.0f) * std::pow (swell, exponent);
    smoothedRate += (target - smoothedRate) * (1.0f - std::exp (-dt / 0.25f));
    rainRate = smoothedRate;

    const float previousGrainRate = grainRate;
    const bool audible = level > 0.0f && (overlay > 0.0f || fuse > 0.0f);

    if (rainRate < 0.01f || ! audible)
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

    //==========================================================================
    // Overlay follows the piano: density and level track its envelope, spectra lean to its centre.
    const float follow = clampf (s.follow, 0.0f, 1.0f);
    const float pianoEnv = clampf (piano.envelope, 0.0f, 1.5f);
    followFactor = (1.0f - follow) + follow * pianoEnv;
    pianoCentroid = piano.centroidHz;
    spectralLean = pianoCentroid > 50.0f ? 0.5f * follow * std::min (pianoEnv, 1.0f) : 0.0f;

    auto lean = [this] (float hz)
    {
        return spectralLean > 0.0f ? std::pow (hz, 1.0f - spectralLean) * std::pow (pianoCentroid, spectralLean) : hz;
    };

    // The distant-rain bed - thousands of far drops merged into a soft wash, tilted between pink and
    // white - carries most of the sound of rain. It brightens with impact speed: a low hiss in a lull,
    // ~2 kHz centre in steady wind-driven rain.
    const float hissCutoff = lean (1500.0f * std::pow (std::max (meanImpactSpeed, 0.5f), 0.65f));
    hissL.set (hissCutoff, 0.6f, fs);
    hissR.set (hissCutoff * 1.04f, 0.6f, fs);

    const float density = grainRate > 0.0f ? std::sqrt (grainRate / 200.0f) * std::sqrt (meanImpactSpeed / 4.0f) : 0.0f;
    hissGain.setTarget (0.025f * density * overlay * std::sqrt (followFactor), std::max (1, (int) (dt * fs)));

    //==========================================================================
    // Fused amounts, all relative to the piano's own sound.
    fusedHiss   = 0.3f * fuse * level * std::min (density, 2.0f);
    patterDepth = 1.5f * fuse * level;

    // Drops on strings: the injected power (fraction of the strike's energy per second) is set by
    // the texture amount, not by how many drops fall - more rain means more, smaller taps.
    const float injectedPower = 2.0f * fuse * level * std::min (density, 2.0f);
    fusedDropGain = grainRate > 0.0f ? std::sqrt (injectedPower / grainRate) : 0.0f;
}

int RainTexture::beginBlock (int numSamples) noexcept
{
    numEvents = 0;
    if (grainRate <= 0.0f)
        return 0;

    while (samplesToNextDrop < (float) numSamples && numEvents < kMaxEvents)
    {
        const float d = atmos::sampleImpactDiameter (lambda, rng);
        const float vt = atmos::terminalVelocity (d);
        const float speed = atmos::impactSpeed (vt, windSpeed);

        // Impact: kinetic energy ~ D^3 v^2, so amplitude ~ D^1.5 v (compressed for audibility).
        const float energyRef = std::pow (d / 2.0f, 1.5f) * (speed / 6.5f);
        const float amp = 0.5f * std::pow (energyRef, 0.7f) * (0.7f + 0.6f * rng.uniform());

        pending[numEvents] = { d, speed, atmos::slantSine (vt, windSpeed), amp };
        events[numEvents] = { std::max (0, (int) samplesToNextDrop), amp,
                              clampf (impactCentreHz (speed) * (0.8f + 0.4f * rng.uniform()), 200.0f, 0.42f * fs) };
        ++numEvents;

        samplesToNextDrop += -std::log (rng.uniformOpen()) * fs / grainRate;
    }

    samplesToNextDrop -= (float) numSamples;
    return numEvents;
}

void RainTexture::spawnGrain (const PendingDrop& drop) noexcept
{
    if (numGrains >= kMaxGrains)
        return;

    auto& g = grains[numGrains++];

    // Where it lands: uniform over the ground around the listener (p(r) ~ r), so most drops are
    // far away - quiet and dull - and only a few land close enough to hear clearly.
    constexpr float rMin = 0.4f, rMax = 15.0f;
    const float r = std::sqrt (rMin * rMin + rng.uniform() * (rMax * rMax - rMin * rMin));
    const float distanceGain = std::min (1.0f, 0.8f / r);

    const float tau = 0.0008f + 0.0018f * drop.diameter;
    constexpr float attackTau = 0.00025f;
    g.decayEnv  = drop.amp * distanceGain;
    g.attackEnv = g.decayEnv;
    g.decayMul  = std::exp (-1.0f / (tau * fs));
    g.attackMul = std::exp (-1.0f / (attackTau * fs));
    g.sign = rng.uniform() < 0.5f ? -1.0f : 1.0f;
    g.patterGain = 1.0f / distanceGain; // fused patter: drops land on the piano, not out in the garden

    // Overlay: louder piano -> denser, louder rain (thinning by the follow factor).
    g.audible = (overlay > 0.0f && rng.uniform() < followFactor) ? overlay * std::sqrt (std::max (followFactor, 1.0f)) : 0.0f;

    // Body: the surface's soft "pat" - lower for bigger drops (a few hundred Hz).
    float bodyHz = 500.0f * std::pow (1.0f / std::max (drop.diameter, 0.2f), 0.4f) * (0.8f + 0.45f * rng.uniform());
    // Tick: the impact's own noise, brighter for faster drops, darker the farther away.
    float tickHz = std::min (1200.0f * std::pow (std::max (drop.speed, 0.5f), 0.7f), 16000.0f / (1.0f + r / 1.5f));
    if (spectralLean > 0.0f)
    {
        bodyHz = std::pow (bodyHz, 1.0f - spectralLean) * std::pow (pianoCentroid, spectralLean);
        tickHz = std::pow (tickHz, 1.0f - 0.5f * spectralLean) * std::pow (pianoCentroid, 0.5f * spectralLean);
    }
    g.body.reset();
    g.body.set (clampf (bodyHz, 120.0f, 0.4f * fs), 2.0f, fs);
    g.tick1.reset();
    g.tick2.reset();
    g.tick1.setCutoff (clampf (tickHz, 300.0f, 0.45f * fs), fs);
    g.tick2.setCutoff (clampf (tickHz, 300.0f, 0.45f * fs), fs);
    g.bodyMix = 0.8f;
    g.tickMix = 0.45f * std::pow (clampf (drop.speed / 6.5f, 0.1f, 2.5f), 0.8f);

    // Rain slants downwind: bias the image toward the side the wind blows to.
    const float pan = clampf (rng.bipolar() * (1.0f - 0.5f * drop.slant) + 0.6f * drop.slant, -1.0f, 1.0f);
    panGains (pan, g.gainL, g.gainR);

    float life = tau * 9.2f + 0.004f; // -80 dB, plus the body's ring

    // Larger drops landing close by in water entrain a bubble that rings at its Minnaert frequency.
    g.bubbleEnv = g.bubbleAttack = 0.0f;
    const float bubbleChance = surface * (drop.diameter > 1.0f ? 0.45f : 0.1f) * (r < 5.0f ? 1.0f : 0.0f);
    if (g.audible > 0.0f && rng.uniform() < bubbleChance)
    {
        const float radius = drop.diameter * (0.25f + 0.35f * rng.uniform());
        const float f = std::min (atmos::minnaertFrequency (radius), 0.4f * fs);
        const float bubbleTau = 0.008f * std::sqrt (2000.0f / f);
        g.bubblePhase = 0.0f;
        g.bubbleInc = f / fs;
        g.bubbleChirp = std::exp (std::log (1.0f + 0.15f + 0.2f * rng.uniform()) / (bubbleTau * 4.0f * fs));
        g.bubbleEnv = g.bubbleAttack = drop.amp * distanceGain * 0.5f * surface;
        g.bubbleDecay = std::exp (-1.0f / (bubbleTau * fs));
        g.bubbleAttackMul = std::exp (-1.0f / (0.0005f * fs));
        life = std::max (life, bubbleTau * 9.2f);
    }

    g.samplesLeft = (int) (life * fs) + 1;
}

void RainTexture::render (float* left, float* right, float* patterLeft, float* patterRight, int numSamples) noexcept
{
    const float outGain = 3.15f * level * std::sqrt (level);
    int nextEvent = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        while (nextEvent < numEvents && events[nextEvent].offset <= i)
            spawnGrain (pending[nextEvent++]);

        float l = 0.0f, r = 0.0f, pl = 0.0f, pr = 0.0f;

        for (int k = 0; k < numGrains;)
        {
            auto& g = grains[k];
            const float env = g.decayEnv - g.attackEnv;
            g.decayEnv  *= g.decayMul;
            g.attackEnv *= g.attackMul;

            // Fused patter: the impact envelope as if the drop hit the piano itself (compressed,
            // ~0.1 for a typical drop), random polarity, panned like the grain.
            const float patter = g.sign * 0.1f * std::min (env * g.patterGain * (1.0f / kTypicalDropAmp), 3.0f);
            pl += patter * g.gainL;
            pr += patter * g.gainR;

            if (g.audible > 0.0f)
            {
                const float n = rng.bipolar() * env * noiseScale;
                g.body.process (n);
                const float tick = g.tick2.process (g.tick1.process (n));
                float y = g.bodyMix * g.body.bandNormalised() + g.tickMix * tick;

                if (g.bubbleEnv > 0.0f)
                {
                    y += fastSinCycles (g.bubblePhase) * (g.bubbleEnv - g.bubbleAttack);
                    g.bubblePhase += g.bubbleInc;
                    if (g.bubblePhase >= 1.0f) g.bubblePhase -= 1.0f;
                    g.bubbleInc = std::min (g.bubbleInc * g.bubbleChirp, 0.45f);
                    g.bubbleEnv    *= g.bubbleDecay;
                    g.bubbleAttack *= g.bubbleAttackMul;
                }

                y *= g.audible;
                l += y * g.gainL;
                r += y * g.gainR;
            }

            if (--g.samplesLeft <= 0)
                grains[k] = grains[--numGrains];
            else
                ++k;
        }

        const float hg = hissGain.next();
        if (hg > 0.0f)
        {
            const float wl = rng.bipolar(), wr = rng.bipolar();
            hissL.process ((0.5f * pinkL.process (wl) + 0.15f * wl) * noiseScale); // ~ -1.5 dB/octave
            hissR.process ((0.5f * pinkR.process (wr) + 0.15f * wr) * noiseScale);
            l += hissHpL.process (hissL.lp) * hg;
            r += hissHpR.process (hissR.lp) * hg;
        }

        left[i]  += l * outGain;
        right[i] += r * outGain;
        patterLeft[i]  = pl;
        patterRight[i] = pr;
    }
}

} // namespace petrichor
