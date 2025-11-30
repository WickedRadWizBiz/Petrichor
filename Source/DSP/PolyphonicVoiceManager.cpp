#include "PolyphonicVoiceManager.h"

PolyphonicVoiceManager::PolyphonicVoiceManager() {}

void PolyphonicVoiceManager::prepare(double sampleRate)
{
    fs = sampleRate;
    for (auto& v : voices)
    {
        v.stringModel.prepare(sampleRate);
        v.active = false;
    }
}

int PolyphonicVoiceManager::findFreeVoice()
{
    // 1. Look for inactive
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        if (!voices[i].active) return i;
    }

    // 2. Steal oldest/quietest
    // Simple: steal oldest
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

    // Reset voice
    voices[idx].active = true;
    voices[idx].age = 0.0f;
    voices[idx].noteNumber = noteNumber;
    voices[idx].velocity = velocity;
    voices[idx].stringModel.reset();

    // Set Params
    float freq = 440.0f * std::pow(2.0f, (noteNumber - 69.0f) / 12.0f);

    // Stiffness B varies by pitch naturally (Bass stiff, Treble flexible)
    // Simple mapping:
    // A0 (21) -> B=0.005
    // C8 (108) -> B=0.0001
    float baseB = juce::jmap((float)noteNumber, 21.0f, 108.0f, 0.005f, 0.0001f);

    // Decay varies by pitch
    float T60 = juce::jmap((float)noteNumber, 21.0f, 108.0f, 5.0f, 0.5f);

    voices[idx].stringModel.setParameters(freq, baseB, T60);

    // Excitation (Hammer/Drop)
    // Just a simple impulse for now, scaled by velocity
    // In a real DWG, we might feed this into the delay line over a few samples (Lowpass Excitation)
    // Hunt-Crossley is continuous interaction, here we just ping it.
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

    for (int s = 0; s < numSamples; ++s)
    {
        float mixL = 0.0f;
        float mixR = 0.0f;

        for (auto& v : voices)
        {
            if (v.active)
            {
                // Apply mods
                // We should probably optimize this to not call every sample if mod is slow
                // but for VST, per-sample is safest for audio rate modulation (Thunder).

                // Base stiffness * Mod (Thunder is a Mod)
                // Wait, Thunder only affects A0-B0.
                // Logic should be in AudioProcessor or passed more specifically.
                // Assuming stiffnessMod is global scaler.

                // Process
                float out = v.stringModel.process(0.0f); // Excitation only on noteOn for now

                mixL += out;
                mixR += out;

                v.age += 1.0f/fs;

                // Auto-kill if silent (optimization)
                if (v.age > 5.0f && std::abs(out) < 0.001f) // crude
                    v.active = false;
            }
        }

        left[s] += mixL * 0.1f; // headroom
        right[s] += mixR * 0.1f;
    }
}
