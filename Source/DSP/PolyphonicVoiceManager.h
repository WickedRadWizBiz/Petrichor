#pragma once
#include <JuceHeader.h>
#include "PhysicalString.h"
#include <array>

// Limit max polyphony for safety
constexpr int MAX_VOICES = 128; // 500 might be too CPU heavy for this MVP without SIMD intrinsics

class PolyphonicVoiceManager
{
public:
    PolyphonicVoiceManager();
    void prepare(double sampleRate);

    // Trigger a new note/drop
    void noteOn(int noteNumber, float velocity, float hammerHardness);

    // Process audio block
    void process(juce::AudioBuffer<float>& buffer,
                 float stiffnessMod,
                 float decayMod,
                 float pitchMod,
                 const std::vector<float>& windGains,
                 const std::vector<float>& windFreqs);

private:
    struct Voice
    {
        bool active = false;
        PhysicalString stringModel;
        int noteNumber = 0;
        float velocity = 0.0f;
        float age = 0.0f; // For stealing logic
    };

    std::array<Voice, MAX_VOICES> voices;
    double fs = 44100.0;

    // Helper to find a free voice or steal
    int findFreeVoice();
};
