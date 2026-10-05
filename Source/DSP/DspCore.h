#pragma once

// Small, allocation-free DSP building blocks shared by the Petrichor engine.
// Deliberately free of JUCE so the engine can be unit tested and rendered offline.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>

#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
 #include <xmmintrin.h>
 #define PETRICHOR_HAS_SSE 1
#endif

namespace petrichor
{

constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

inline float clampf (float x, float lo, float hi) noexcept { return std::min (hi, std::max (lo, x)); }
inline float lerpf (float a, float b, float t) noexcept     { return a + (b - a) * t; }
inline float dbToGain (float db) noexcept                   { return std::pow (10.0f, db * 0.05f); }
inline float midiToHz (float note, float a4 = 440.0f) noexcept
{
    return a4 * std::exp2 ((note - 69.0f) / 12.0f);
}

/** Flushes denormals to zero for the lifetime of the object (x86 only; a no-op elsewhere). */
class DenormalGuard
{
public:
    DenormalGuard() noexcept
    {
       #if PETRICHOR_HAS_SSE
        saved = _mm_getcsr();
        _mm_setcsr (saved | 0x8040); // FTZ | DAZ
       #endif
    }
    ~DenormalGuard() noexcept
    {
       #if PETRICHOR_HAS_SSE
        _mm_setcsr (saved);
       #endif
    }
private:
   #if PETRICHOR_HAS_SSE
    unsigned int saved = 0;
   #endif
};

/** xoshiro128+ : fast, decent-quality PRNG that is safe to use on the audio thread. */
class Rng
{
public:
    explicit Rng (uint32_t seed = 0x9E3779B9u) noexcept { setSeed (seed); }

    void setSeed (uint32_t seed) noexcept
    {
        uint32_t z = seed ? seed : 0x9E3779B9u;
        for (auto& s : state)
        {
            z += 0x9E3779B9u;
            uint32_t t = z;
            t = (t ^ (t >> 16)) * 0x85EBCA6Bu;
            t = (t ^ (t >> 13)) * 0xC2B2AE35u;
            s = t ^ (t >> 16);
        }
        hasSpareGaussian = false;
    }

    uint32_t nextU32() noexcept
    {
        const uint32_t result = state[0] + state[3];
        const uint32_t t = state[1] << 9;
        state[2] ^= state[0];
        state[3] ^= state[1];
        state[1] ^= state[2];
        state[0] ^= state[3];
        state[2] ^= t;
        state[3] = (state[3] << 11) | (state[3] >> 21);
        return result;
    }

    /** Uniform in [0, 1). */
    float uniform() noexcept { return (float) (nextU32() >> 8) * (1.0f / 16777216.0f); }

    /** Uniform in [-1, 1). */
    float bipolar() noexcept { return uniform() * 2.0f - 1.0f; }

    /** Uniform in (0, 1] - safe for log(). */
    float uniformOpen() noexcept { return ((float) (nextU32() >> 8) + 1.0f) * (1.0f / 16777216.0f); }

    /** Standard normal deviate (Box-Muller, pairs cached). */
    float gaussian() noexcept
    {
        if (hasSpareGaussian)
        {
            hasSpareGaussian = false;
            return spareGaussian;
        }
        const float r = std::sqrt (-2.0f * std::log (uniformOpen()));
        const float a = kTwoPi * uniform();
        spareGaussian = r * std::sin (a);
        hasSpareGaussian = true;
        return r * std::cos (a);
    }

private:
    uint32_t state[4] {};
    float spareGaussian = 0.0f;
    bool hasSpareGaussian = false;
};

/** Stateless hash -> [0,1). Used for per-key "manufacturing" randomness that must be repeatable. */
inline float hash01 (uint32_t a, uint32_t b = 0) noexcept
{
    uint32_t h = a * 0x8DA6B343u ^ (b + 0x632BE59Bu) * 0xD8163841u;
    h ^= h >> 15; h *= 0x2C1B3C6Du;
    h ^= h >> 12; h *= 0x297A2D39u;
    h ^= h >> 15;
    return (float) (h >> 8) * (1.0f / 16777216.0f);
}

/** One-pole low-pass, y += a (x - y). */
struct OnePoleLP
{
    float a = 1.0f, y = 0.0f;

    void setCutoff (float hz, float fs) noexcept
    {
        a = (hz >= 0.49f * fs) ? 1.0f : 1.0f - std::exp (-kTwoPi * hz / fs);
    }
    void setTimeConstant (float seconds, float fs) noexcept
    {
        a = seconds <= 0.0f ? 1.0f : 1.0f - std::exp (-1.0f / (seconds * fs));
    }
    float process (float x) noexcept { y += a * (x - y); return y; }
    void reset (float v = 0.0f) noexcept { y = v; }
};

/** One-pole high-pass (DC blocker style). */
struct OnePoleHP
{
    OnePoleLP lp;
    void setCutoff (float hz, float fs) noexcept { lp.setCutoff (hz, fs); }
    float process (float x) noexcept { return x - lp.process (x); }
    void reset() noexcept { lp.reset(); }
};

/** Topology-preserving-transform state variable filter (Zavalishin). */
struct Svf
{
    float g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
    float lp = 0.0f, bp = 0.0f, hp = 0.0f;

    void set (float hz, float q, float fs) noexcept
    {
        hz = clampf (hz, 5.0f, 0.48f * fs);
        g  = std::tan (kPi * hz / fs);
        k  = 1.0f / std::max (q, 0.05f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void process (float x) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2;
        bp = v1;
        hp = x - k * v1 - v2;
    }

    /** Band-pass normalised to unity gain at the centre frequency. */
    float bandNormalised() const noexcept { return bp * k; }

    void reset() noexcept { ic1 = ic2 = lp = bp = hp = 0.0f; }
};

/** Linear parameter smoother evaluated once per sample. */
struct Ramp
{
    float value = 0.0f, target = 0.0f, step = 0.0f;
    int remaining = 0;

    void reset (float v) noexcept { value = target = v; step = 0.0f; remaining = 0; }
    void setTarget (float t, int samples) noexcept
    {
        target = t;
        if (samples <= 0) { value = t; remaining = 0; step = 0.0f; return; }
        remaining = samples;
        step = (target - value) / (float) samples;
    }
    float next() noexcept
    {
        if (remaining > 0) { value += step; if (--remaining == 0) value = target; }
        return value;
    }
};

/** Cubic Hermite (Catmull-Rom) interpolation between y1 and y2. */
inline float hermite (float y0, float y1, float y2, float y3, float t) noexcept
{
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

/** Equal-power pan law; pan in [-1, 1]. */
inline void panGains (float pan, float& l, float& r) noexcept
{
    const float a = (clampf (pan, -1.0f, 1.0f) + 1.0f) * (kPi * 0.25f);
    l = std::cos (a);
    r = std::sin (a);
}

} // namespace petrichor
