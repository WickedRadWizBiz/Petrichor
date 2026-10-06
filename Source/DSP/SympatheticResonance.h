#pragma once

#include <array>
#include <vector>
#include "DspCore.h"

namespace petrichor
{

/**
    Sympathetic string resonance, after the digital-piano idea of "virtual resonance modelling":
    the undamped strings of a piano ring along with whatever is played, which is a large part of
    what makes a piano sound alive rather than "choppy and music-box like".

    The free strings are a bank of tuned feedback combs - one per semitone across the low two
    octaves (A1..G#3), whose harmonic series cover the overtones of everything above - each with a
    one-pole loss filter so upper harmonics die first. With the sustain pedal down every string is
    free and rings long; with it up only a short, faint resonance remains.
*/
class SympatheticResonance
{
public:
    static constexpr int kStrings = 24;
    static constexpr int kLowestKey = 33; // A1

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        const int maxDelay = (int) std::ceil (fs / 40.0f) + 4;
        for (auto& s : strings)
        {
            s.line.assign ((size_t) maxDelay, 0.0f);
            s.write = 0;
            s.loss.reset();
        }
        tuning = -1.0f;
        pedalAmount = 0.0f;
        setTuning (440.0f);
        inputHp.setCutoff (60.0f, fs);
    }

    void setTuning (float a4Hz) noexcept
    {
        if (std::abs (a4Hz - tuning) < 1.0e-3f)
            return;
        tuning = a4Hz;
        for (int i = 0; i < kStrings; ++i)
        {
            auto& s = strings[(size_t) i];
            const float hz = midiToHz ((float) (kLowestKey + i), a4Hz);
            // The one-pole loss filter adds about half a sample of delay at low frequencies.
            s.delay = std::min ((float) s.line.size() - 2.0f, fs / hz - 0.5f);
            s.loss.setCutoff (2600.0f, fs);
            s.pan = (i & 1) ? 0.62f : 0.38f;
        }
        lastRt60 = -1.0f;
    }

    /** amount 0..1 (overall resonance), pedalDown frees every string. Control rate. */
    void setState (float amount01, bool pedalDown, float dt) noexcept
    {
        amount = clampf (amount01, 0.0f, 1.0f);
        const float target = pedalDown ? 1.0f : 0.0f;
        pedalAmount += (target - pedalAmount) * (1.0f - std::exp (-dt / 0.12f));

        // Pedal down: strings ring ~3.5 s; up: only a brief ~0.5 s bloom (dampers resting on them).
        const float rt60 = lerpf (0.5f, 3.5f, pedalAmount);
        if (std::abs (rt60 - lastRt60) > 0.01f)
        {
            lastRt60 = rt60;
            for (auto& s : strings)
                s.feedback = std::pow (10.0f, -3.0f * s.delay / (fs * rt60));
        }
        outputGain = amount * lerpf (0.25f, 1.0f, pedalAmount) * 0.2f;
    }

    /** Adds the resonance of `mono` (the dry piano) into left / right. */
    void process (const float* mono, float* left, float* right, int numSamples) noexcept
    {
        if (outputGain <= 1.0e-6f)
            return;

        for (int n = 0; n < numSamples; ++n)
        {
            const float x = inputHp.process (mono[n]);
            float l = 0.0f, r = 0.0f;

            for (auto& s : strings)
            {
                const int size = (int) s.line.size();
                float readPos = (float) s.write - s.delay;
                if (readPos < 0.0f)
                    readPos += (float) size;
                const int i0 = (int) readPos;
                const int i1 = (i0 + 1) % size;
                const float frac = readPos - (float) i0;
                const float delayed = s.line[(size_t) i0] + frac * (s.line[(size_t) i1] - s.line[(size_t) i0]);

                const float y = s.loss.process (delayed) * s.feedback;
                s.line[(size_t) s.write] = y + x * (1.0f - s.feedback);
                s.write = (s.write + 1) % size;

                l += y * (1.0f - s.pan);
                r += y * s.pan;
            }

            left[n]  += l * outputGain;
            right[n] += r * outputGain;
        }
    }

    void clear() noexcept
    {
        for (auto& s : strings)
        {
            std::fill (s.line.begin(), s.line.end(), 0.0f);
            s.loss.reset();
        }
    }

private:
    struct String
    {
        std::vector<float> line;
        int write = 0;
        float delay = 100.0f, feedback = 0.0f, pan = 0.5f;
        OnePoleLP loss;
    };

    float fs = 48000.0f, tuning = 440.0f;
    float amount = 0.0f, pedalAmount = 0.0f, outputGain = 0.0f, lastRt60 = -1.0f;
    std::array<String, kStrings> strings;
    OnePoleHP inputHp;
};

} // namespace petrichor
