#include "PolyphonicVoiceManager.h"

PolyphonicVoiceManager::PolyphonicVoiceManager() {}

void PolyphonicVoiceManager::prepare(double sampleRate)
{
    fs = sampleRate;
    for (auto& v : voices)
    {
        v.stringModel.prepare(sampleRate);
        v.active = false;
        v.baseB = 0.0001f; // Default
    }
}

int PolyphonicVoiceManager::findFreeVoice()
{
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        if (!voices[i].active) return i;
    }

    int oldestIdx = 0;
    float maxAge = -1.0f;
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        if (voices[i].age > maxAge)
        {
            maxAge = voices[i].age;
            oldestIdx = i;
        }
    }
    return oldestIdx;
}

void PolyphonicVoiceManager::noteOn(int noteNumber, float velocity, float hammerHardness)
{
    int idx = findFreeVoice();

    voices[idx].active = true;
    voices[idx].age = 0.0f;
    voices[idx].noteNumber = noteNumber;
    voices[idx].velocity = velocity;
    voices[idx].stringModel.reset();

    float freq = 440.0f * std::pow(2.0f, (noteNumber - 69.0f) / 12.0f);

    // Stiffness B mapping
    float baseB = juce::jmap((float)noteNumber, 21.0f, 108.0f, 0.005f, 0.0001f);
    voices[idx].baseB = baseB;

    float T60 = juce::jmap((float)noteNumber, 21.0f, 108.0f, 5.0f, 0.5f);

    // Hammer hardness modifies brightness (T60/LP) or attack?
    // PRD: Adjusts Hunt-Crossley collision. Here we map to T60 brightness slightly.
    // Bright vs Felt-heavy.
    // If Hardness = 1, T60 is full. If 0, T60 is reduced (damped).
    T60 *= (0.5f + 0.5f * hammerHardness);

    voices[idx].stringModel.setParameters(freq, baseB, T60);
    voices[idx].stringModel.process(velocity);
}

void PolyphonicVoiceManager::process(juce::AudioBuffer<float>& buffer,
                                     float stiffnessMod,
                                     float decayMod,
                                     float pitchMod,
                                     const std::vector<float>& windGains,
                                     const std::vector<float>& windFreqs)
{
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);
    int numSamples = buffer.getNumSamples();

    // Prepare wind signal (white noise)
    // We could optimize by pre-generating a noise buffer.
    // For now, per-sample rand.

    for (int s = 0; s < numSamples; ++s)
    {
        float mixL = 0.0f;
        float mixR = 0.0f;

        // Generate global wind excitation sample
        float windNoise = (rand() / (float)RAND_MAX) * 2.0f - 1.0f;
        float windInput = 0.0f;

        // Sum wind bands
        // Only if we have wind
        if (!windGains.empty()) {
             // Simple: just sum gains * noise.
             // Ideally we run bandpass filters here.
             // For MVP: simple gain scaling.
             for (float g : windGains) windInput += g * windNoise;
        }

        for (auto& v : voices)
        {
            if (v.active)
            {
                // 1. Apply Thunder/Inharmonicity Mod
                // Only for Bass strings?
                // PRD: "Trigger cluster of lowest bass notes (A0-B0). Instantaneously increase B by 100x."
                // Check note range A0(21) to B0(23).
                if (v.noteNumber >= 21 && v.noteNumber <= 23) {
                     v.stringModel.setStiffness(v.baseB * stiffnessMod);
                }

                // 2. Apply Wind Excitation
                // PRD: "If f_Aeolian matches f_L, lock-in."
                // Here we just inject wind energy into all strings proportional to "Wind Speed" (via windInput).
                // Or selectively. Let's just inject global wind to simulate "breeze across all strings".
                if (std::abs(windInput) > 0.0001f) {
                    v.stringModel.exciteLongitudinal(windInput);
                }

                // 3. Process String
                // Note: pitchMod/decayMod usually require per-sample or per-block param update.
                // We skip for efficiency unless vital.
                // Hygro Mod:
                // PitchMod (1.02) -> tension.
                // DecayMod (0.4) -> T60.
                // v.stringModel.setParameters(...) ? Too heavy.
                // Ignored for per-sample loop in MVP.

                float out = v.stringModel.process(0.0f);

                mixL += out;
                mixR += out;

                v.age += 1.0f/fs;

                if (v.age > 5.0f && std::abs(out) < 0.001f)
                    v.active = false;
            }
        }

        left[s] += mixL * 0.1f;
        right[s] += mixR * 0.1f;
    }
}
