#pragma once

// The atmospheric mathematics behind Petrichor Piano.
// Every mapping in the engine goes through one of these functions so the physics stays in one place.

#include "DspCore.h"

namespace petrichor::atmos
{

//==============================================================================
// Physical constants
constexpr float kGravity          = 9.81f;   // m/s^2
constexpr float kWaterDensity     = 1000.0f; // kg/m^3
constexpr float kAirDensity       = 1.225f;  // kg/m^3
constexpr float kDragCoefficient  = 0.5f;    // sphere, Re ~ 10^3
constexpr float kSpeedOfSound     = 343.0f;  // m/s
constexpr float kStrouhal         = 0.2f;    // vortex shedding behind a cylinder

//==============================================================================
// Rain: Marshall-Palmer drop size distribution  N(D) = N0 exp(-Lambda D)

constexpr float kMarshallPalmerN0 = 8000.0f; // m^-3 mm^-1
constexpr float kMinDropMM        = 0.1f;
constexpr float kMaxDropMM        = 6.0f;    // larger drops break up

/** Lambda = 4.1 R^-0.21  [mm^-1], R = rainfall rate in mm/h. */
inline float marshallPalmerLambda (float rainRateMMh) noexcept
{
    return 4.1f * std::pow (std::max (rainRateMMh, 0.01f), -0.21f);
}

/** N(D) in m^-3 mm^-1 for a drop diameter in mm. */
inline float marshallPalmerDensity (float diameterMM, float rainRateMMh) noexcept
{
    return kMarshallPalmerN0 * std::exp (-marshallPalmerLambda (rainRateMMh) * diameterMM);
}

//==============================================================================
// Terminal velocity  v_T(D) = sqrt(4 g rho_w D / (3 rho_a C_d))

/** Observed fall speeds saturate around 9.3 m/s once drops flatten (Gunn & Kinzer). */
constexpr float kMaxTerminalVelocity = 9.3f;

inline float terminalVelocity (float diameterMM) noexcept
{
    const float d = std::max (diameterMM, 0.0f) * 1.0e-3f;
    const float v = std::sqrt ((4.0f * kGravity * kWaterDensity * d) / (3.0f * kAirDensity * kDragCoefficient));
    return std::min (v, kMaxTerminalVelocity);
}

/** Wind advection: the drop's trajectory is the vector sum of the wind and its terminal velocity. */
inline float impactSpeed (float terminalVelocityMs, float windSpeedMs) noexcept
{
    return std::sqrt (terminalVelocityMs * terminalVelocityMs + windSpeedMs * windSpeedMs);
}

/** Sine of the slant angle from vertical; positive leans downwind. */
inline float slantSine (float terminalVelocityMs, float windSpeedMs) noexcept
{
    const float s = impactSpeed (terminalVelocityMs, windSpeedMs);
    return s > 1.0e-6f ? windSpeedMs / s : 0.0f;
}

/** Drop arrival flux through a horizontal surface: F = integral N(D) v_T(D) dD  [drops m^-2 s^-1]. */
inline float dropNumberFlux (float rainRateMMh) noexcept
{
    if (rainRateMMh <= 0.0f)
        return 0.0f;

    const float lambda = marshallPalmerLambda (rainRateMMh);
    constexpr int intervals = 32; // Simpson's rule, even
    const float h = (kMaxDropMM - kMinDropMM) / (float) intervals;
    float sum = 0.0f;

    for (int i = 0; i <= intervals; ++i)
    {
        const float d = kMinDropMM + h * (float) i;
        const float f = kMarshallPalmerN0 * std::exp (-lambda * d) * terminalVelocity (d);
        const float w = (i == 0 || i == intervals) ? 1.0f : ((i & 1) ? 4.0f : 2.0f);
        sum += w * f;
    }
    return sum * h / 3.0f;
}

/** Samples a diameter from the flux-weighted distribution N(D) v_T(D) on [kMinDropMM, kMaxDropMM].
    Larger drops fall faster, so they reach the ground more often than their concentration suggests. */
inline float sampleImpactDiameter (float lambda, Rng& rng) noexcept
{
    const float span = kMaxDropMM - kMinDropMM;
    const float tailMass = 1.0f - std::exp (-lambda * span);

    for (int attempt = 0; attempt < 16; ++attempt)
    {
        const float u = rng.uniform();
        const float d = kMinDropMM - std::log (1.0f - u * tailMass) / lambda;
        if (rng.uniform() * kMaxTerminalVelocity <= terminalVelocity (d))
            return d;
    }
    return kMinDropMM - std::log (1.0f - rng.uniform() * tailMass) / lambda;
}

/** Minnaert resonance of an entrained bubble, f ~ 3.26 / a  (a = radius in metres). */
inline float minnaertFrequency (float bubbleRadiusMM) noexcept
{
    return 3.26f / std::max (bubbleRadiusMM * 1.0e-3f, 1.0e-5f);
}

//==============================================================================
// Wind

/** Aeolian / vortex-shedding tone  f = St U / L. */
inline float strouhalFrequency (float windSpeedMs, float lengthM) noexcept
{
    return kStrouhal * windSpeedMs / std::max (lengthM, 1.0e-6f);
}

/** The obstacle length a piano note maps to: its own acoustic wavelength, L = c / f0.
    Low keys are large obstacles (slow shedding), high keys are small ones (fast shedding). */
inline float noteObstacleLength (float fundamentalHz) noexcept
{
    return kSpeedOfSound / std::max (fundamentalHz, 1.0f);
}

/** Corner frequency of the von Karman gust spectrum S(f) ~ 1 / (1 + 70.8 (f L / U)^2)^(5/6).
    Above it the spectrum follows Kolmogorov's -5/3 law. */
inline float vonKarmanCornerHz (float windSpeedMs, float integralLengthM) noexcept
{
    return windSpeedMs / (std::max (integralLengthM, 0.1f) * 8.414f); // sqrt(70.8)
}

/** Doppler ratio for a gust component u (m/s) moving the source toward the listener. */
inline float dopplerRatio (float lineOfSightVelocityMs) noexcept
{
    return kSpeedOfSound / (kSpeedOfSound - clampf (lineOfSightVelocityMs, -60.0f, 60.0f));
}

//==============================================================================
// Thunder: atmospheric absorption  P(f, x) = P0(f) exp(-alpha(f) x),  alpha ~ f^2

/** alpha at 1 kHz, nepers per metre (~0.004 dB/m, mid humidity). */
constexpr float kAbsorptionAt1kHz = 4.6e-4f;

inline float absorptionCoefficient (float hz) noexcept
{
    const float k = hz * 1.0e-3f;
    return kAbsorptionAt1kHz * k * k;
}

/** Linear pressure gain after travelling x metres. */
inline float absorptionGain (float hz, float distanceM) noexcept
{
    return std::exp (-absorptionCoefficient (hz) * std::max (distanceM, 0.0f));
}

/** For a cascade of N identical one-pole low-passes, ln|H| ~ -(N/2)(f/fc)^2, which matches the
    Gaussian exp(-alpha x) roll-off when fc = 1 kHz * sqrt(N / (2 alpha_1k x)). */
inline float absorptionCascadeCutoff (float distanceM, int stages) noexcept
{
    if (distanceM <= 1.0e-3f)
        return 1.0e9f;
    return 1000.0f * std::sqrt ((float) stages / (2.0f * kAbsorptionAt1kHz * distanceM));
}

/** MIDI velocity as distance to the strike: x = xMax (127 - V) / 126. */
inline float velocityToDistance (float midiVelocity, float maxDistanceM) noexcept
{
    return maxDistanceM * clampf ((127.0f - midiVelocity) / 126.0f, 0.0f, 1.0f);
}

} // namespace petrichor::atmos
