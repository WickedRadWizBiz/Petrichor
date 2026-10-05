#pragma once

#include "Atmosphere.h"
#include "KolmogorovNoise.h"

namespace petrichor
{

/** Instantaneous state of the storm's wind, shared by every voice and the rain each control tick. */
struct WindState
{
    float meanSpeed  = 0.0f; // U_mean, m/s
    float speed      = 0.0f; // U(t) = U_mean * gustFactor
    float gust       = 0.0f; // G(t): unit-variance von Karman turbulence
    float gustFactor = 1.0f; // 1 + I G(t), >= 0 - the absolute amplitude of the wind LFO
    float intensity  = 0.0f; // turbulence intensity I = sigma_u / U_mean
};

/**
    The storm's large-scale wind: U(t) = U_mean (1 + I G(t)).

    G(t) is von Karman turbulence whose corner sits at U / (8.41 L) for an integral length scale L
    (Taylor's frozen-turbulence hypothesis): stronger wind or smaller eddies give faster gusts.
*/
class StormWind
{
public:
    void reset (uint32_t seed) noexcept
    {
        noise.reset (seed);
        smoothedMean = targetMean;
    }

    void setParameters (float meanSpeedMs, float turbulence01, float gustLengthM) noexcept
    {
        targetMean    = std::max (meanSpeedMs, 0.0f);
        intensity     = 0.6f * clampf (turbulence01, 0.0f, 1.0f);
        integralScale = std::max (gustLengthM, 0.5f);
    }

    /** Advance the wind by dt seconds (called at control rate). */
    const WindState& tick (float dt) noexcept
    {
        // Glide the mean so automation sweeps sound like weather, not steps.
        const float glide = 1.0f - std::exp (-dt / 0.35f);
        smoothedMean += (targetMean - smoothedMean) * glide;

        // A calm storm still breathes slowly: keep the gust clock running at >= 1 m/s.
        const float corner = atmos::vonKarmanCornerHz (std::max (smoothedMean, 1.0f), integralScale);
        const float g = noise.advance (corner, dt);

        state.meanSpeed  = smoothedMean;
        state.gust       = g;
        state.intensity  = intensity;
        state.gustFactor = std::max (0.0f, 1.0f + intensity * g);
        state.speed      = smoothedMean * state.gustFactor;
        return state;
    }

    const WindState& current() const noexcept { return state; }

private:
    KolmogorovNoise noise;
    WindState state;
    float targetMean = 0.0f, smoothedMean = 0.0f;
    float intensity = 0.2f, integralScale = 12.0f;
};

} // namespace petrichor
