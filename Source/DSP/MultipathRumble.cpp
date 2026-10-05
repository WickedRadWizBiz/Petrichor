#include "MultipathRumble.h"

namespace petrichor
{

namespace
{
    struct ZoneDesign
    {
        float preDelayMs, spreadMs, toneHz, size, rollDepthMs, rollCornerHz;
    };

    // near: localised soundboard/room resonance; far: long, dark, rolling multipath.
    constexpr ZoneDesign kZoneDesigns[MultipathRumble::kZones] = {
        {  4.0f,  35.0f, 7000.0f, 0.35f, 0.4f, 0.45f },
        { 18.0f, 140.0f, 3200.0f, 0.70f, 2.0f, 0.25f },
        { 45.0f, 420.0f, 1400.0f, 1.15f, 6.0f, 0.12f },
    };

    constexpr int kBaseLineLengths[MultipathRumble::kLines] = { 1427, 1637, 1871, 2053, 2273, 2459, 2687, 2903 };
}

void MultipathRumble::zoneWeights (float d, float (&w)[kZones]) noexcept
{
    // Triangular crossfade across zones at d = 0, 0.5, 1, normalised to equal power.
    d = clampf (d, 0.0f, 1.0f);
    const float a = std::max (0.0f, 1.0f - 2.0f * d);
    const float b = 1.0f - std::abs (2.0f * d - 1.0f);
    const float c = std::max (0.0f, 2.0f * d - 1.0f);
    const float norm = 1.0f / std::sqrt (std::max (a * a + b * b + c * c, 1.0e-9f));
    w[0] = a * norm;
    w[1] = b * norm;
    w[2] = c * norm;
}

void MultipathRumble::prepare (double sampleRate, uint32_t seed)
{
    fs = (float) sampleRate;
    Rng rng (seed);
    const float scale = fs / 48000.0f;

    for (int z = 0; z < kZones; ++z)
    {
        auto& zone = zones[z];
        const auto& d = kZoneDesigns[z];

        zone.toneHz = d.toneHz;
        zone.rollDepthSamples = d.rollDepthMs * 1.0e-3f * fs;
        zone.rollCornerHz = d.rollCornerHz;

        const float pre = d.preDelayMs * 1.0e-3f * fs;
        const float spread = d.spreadMs * 1.0e-3f * fs;
        zone.input.allocate ((int) (pre + spread + 2.0f * zone.rollDepthSamples) + 8);

        // h(tau): arrivals bunched early and thinning out, each path darker than the last.
        float energy = 0.0f;
        for (int t = 0; t < kTaps; ++t)
        {
            auto& tap = zone.taps[t];
            const float slot = ((float) t + 0.5f + 0.6f * (rng.uniform() - 0.5f)) / (float) kTaps;
            const float position = std::pow (clampf (slot, 0.0f, 1.0f), 1.5f);
            tap.baseDelay = pre + spread * position + zone.rollDepthSamples + 1.0f;
            tap.delay = tap.baseDelay;
            tap.gain = std::exp (-2.5f * position) * (0.6f + 0.4f * rng.uniform()) * ((t & 1) ? -1.0f : 1.0f);
            energy += tap.gain * tap.gain;

            const float pan = clampf (((t & 1) ? 0.55f : -0.55f) + 0.4f * rng.bipolar(), -1.0f, 1.0f);
            panGains (pan, tap.gainL, tap.gainR);
            tap.tone.setCutoff (d.toneHz * (1.0f - 0.6f * (float) t / (float) kTaps), fs);
            tap.tone.reset();
            tap.roll.reset (rng.nextU32());
        }
        const float tapNorm = 1.0f / std::sqrt (std::max (energy, 1.0e-9f));
        for (auto& tap : zone.taps)
            tap.gain *= tapNorm;

        for (int l = 0; l < kLines; ++l)
        {
            zone.lengths[l] = std::max (8, (int) ((float) kBaseLineLengths[l] * d.size * scale));
            zone.lines[l].allocate (zone.lengths[l] + 1);
            zone.damping[l].setCutoff (d.toneHz * 1.4f, fs);
            zone.damping[l].reset();
        }
    }

    farRt60 = -1.0f;
    setDecay (5.0f);
}

void MultipathRumble::setZoneRt60 (Zone& zone, float rt60) noexcept
{
    zone.rt60 = std::max (rt60, 0.05f);
    for (int l = 0; l < kLines; ++l)
        zone.feedback[l] = std::pow (10.0f, -3.0f * (float) zone.lengths[l] / (fs * zone.rt60));
}

void MultipathRumble::setDecay (float farRt60Seconds) noexcept
{
    farRt60Seconds = std::max (farRt60Seconds, 0.2f);
    if (std::abs (farRt60Seconds - farRt60) < 1.0e-3f)
        return;

    farRt60 = farRt60Seconds;
    setZoneRt60 (zones[0], std::max (0.25f, 0.15f * farRt60));
    setZoneRt60 (zones[1], 0.45f * farRt60);
    setZoneRt60 (zones[2], farRt60);
}

void MultipathRumble::controlTick (const WindState& wind, float dt) noexcept
{
    // Gusts make the roll livelier.
    const float liveliness = 0.7f + 0.3f * wind.gustFactor;

    for (auto& zone : zones)
    {
        for (auto& tap : zone.taps)
        {
            const float r = tap.roll.advance (zone.rollCornerHz * liveliness, dt);
            tap.delay = tap.baseDelay + zone.rollDepthSamples * clampf (r, -1.0f, 1.0f);
            tap.rollGain = clampf (1.0f + 0.45f * r, 0.2f, 1.9f);
        }
    }
}

void MultipathRumble::process (const float* const* zoneInputs, float* left, float* right, int numSamples) noexcept
{
    constexpr float householder = 2.0f / (float) kLines;
    constexpr float lineOutGain = 0.5f;
    // Output trims so a distant strike's roll sits close to the level of its direct sound.
    constexpr float zoneGain[kZones] = { 0.9f, 1.5f, 2.8f };

    for (int z = 0; z < kZones; ++z)
    {
        auto& zone = zones[z];
        const float* in = zoneInputs[z];
        const float outGain = zoneGain[z];

        for (int i = 0; i < numSamples; ++i)
        {
            zone.input.push (in[i]);

            float l = 0.0f, r = 0.0f, mono = 0.0f;
            for (auto& tap : zone.taps)
            {
                const float y = tap.tone.process (zone.input.readFractional (tap.delay)) * tap.gain * tap.rollGain;
                l += y * tap.gainL;
                r += y * tap.gainR;
                mono += y;
            }

            // Feedback delay network for the diffuse tail.
            float outs[kLines];
            float sum = 0.0f;
            for (int k = 0; k < kLines; ++k)
            {
                outs[k] = zone.damping[k].process (zone.lines[k].read (zone.lengths[k]));
                sum += outs[k];
            }

            const float injection = mono;
            for (int k = 0; k < kLines; ++k)
            {
                const float mixed = outs[k] - householder * sum;
                zone.lines[k].push (mixed * zone.feedback[k] + ((k & 1) ? -injection : injection));
            }

            l += lineOutGain * ((outs[0] - outs[2]) + (outs[4] - outs[6]));
            r += lineOutGain * ((outs[1] - outs[3]) + (outs[5] - outs[7]));

            left[i]  += l * outGain;
            right[i] += r * outGain;
        }
    }
}

void MultipathRumble::clear() noexcept
{
    for (auto& zone : zones)
    {
        std::fill (zone.input.buffer.begin(), zone.input.buffer.end(), 0.0f);
        for (auto& line : zone.lines)
            std::fill (line.buffer.begin(), line.buffer.end(), 0.0f);
        for (auto& d : zone.damping)
            d.reset();
        for (auto& tap : zone.taps)
            tap.tone.reset();
    }
}

} // namespace petrichor
