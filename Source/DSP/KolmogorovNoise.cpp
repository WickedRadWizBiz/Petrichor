#include "KolmogorovNoise.h"

namespace petrichor
{

namespace
{
    // Corner normalised to 1 Hz at an internal rate of kStepsPerCorner Hz.
    constexpr float kInternalRate = (float) KolmogorovNoise::kStepsPerCorner;

    float prewarp (float hz) noexcept
    {
        return 2.0f * kInternalRate * std::tan (kPi * hz / kInternalRate);
    }
}

const KolmogorovNoise::Design& KolmogorovNoise::design() noexcept
{
    static const Design d = []
    {
        Design result;
        const float k = 2.0f * kInternalRate; // bilinear constant

        // Three pole/zero pairs, two per decade. Each zero sits 5/6 of the way to the next pole, so
        // the magnitude falls at 5/6 of 20 dB/decade on average: |H| ~ f^(-5/6), |H|^2 ~ f^(-5/3).
        const float spacing = std::sqrt (10.0f);
        const float zeroOffset = std::pow (spacing, 5.0f / 6.0f);
        float pole = 1.0f;

        for (int i = 0; i < kSections - 1; ++i)
        {
            const float wp = prewarp (pole);
            const float wz = prewarp (pole * zeroOffset);
            const float norm = 1.0f + k / wp;
            result.sections[i].b0 = (1.0f + k / wz) / norm;
            result.sections[i].b1 = (1.0f - k / wz) / norm;
            result.sections[i].a1 = (1.0f - k / wp) / norm;
            pole *= spacing;
        }

        // Dissipation range: a plain pole about a decade above the corner keeps the LFO smooth.
        {
            const float wp = prewarp (12.0f);
            const float norm = 1.0f + k / wp;
            auto& s = result.sections[kSections - 1];
            s.b0 = 1.0f / norm;
            s.b1 = 1.0f / norm;
            s.a1 = (1.0f - k / wp) / norm;
        }

        // Normalise to unit output variance for unit-variance white input: 1 / sqrt(sum h^2).
        float st[kSections] {};
        double energy = 0.0;
        for (int n = 0; n < 16384; ++n)
        {
            float x = (n == 0) ? 1.0f : 0.0f;
            for (int i = 0; i < kSections; ++i)
            {
                const auto& s = result.sections[i];
                const float y = s.b0 * x + st[i];
                st[i] = s.b1 * x - s.a1 * y;
                x = y;
            }
            energy += (double) x * (double) x;
        }
        result.inputGain = (float) (1.0 / std::sqrt (std::max (energy, 1.0e-12)));
        return result;
    }();

    return d;
}

void KolmogorovNoise::reset (uint32_t seed) noexcept
{
    rng.setSeed (seed);
    for (auto& s : state) s = 0.0f;
    phase = 0.0f;

    // Run into steady state so the first output already has the target variance.
    for (int i = 0; i < 1024; ++i)
        step();

    for (auto& h : history)
        h = step();

    output = history[1];
}

float KolmogorovNoise::step() noexcept
{
    const auto& d = design();
    float x = rng.gaussian() * d.inputGain;

    for (int i = 0; i < kSections; ++i)
    {
        const auto& s = d.sections[i];
        const float y = s.b0 * x + state[i];
        state[i] = s.b1 * x - s.a1 * y;
        x = y;
    }
    return x;
}

float KolmogorovNoise::advance (float cornerHz, float dt) noexcept
{
    phase += kInternalRate * std::max (cornerHz, 0.0f) * std::max (dt, 0.0f);

    int guard = 0;
    while (phase >= 1.0f && guard++ < 64)
    {
        phase -= 1.0f;
        history[0] = history[1];
        history[1] = history[2];
        history[2] = history[3];
        history[3] = step();
    }
    phase = std::min (phase, 0.999999f);

    output = hermite (history[0], history[1], history[2], history[3], phase);
    return output;
}

} // namespace petrichor
