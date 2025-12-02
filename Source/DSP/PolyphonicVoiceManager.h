#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "PhysicalString.h"
#include <array>

constexpr int MAX_VOICES = 128;

class PolyphonicVoiceManager
{
public:
    PolyphonicVoiceManager();
    void prepare(double sampleRate);

    void noteOn(int noteNumber, float velocity, float hammerHardness);

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
        float age = 0.0f;
        float baseB = 0.0001f; // Store base stiffness to allow relative modulation
    };

    std::array<Voice, MAX_VOICES> voices;
    double fs = 44100.0;

    int findFreeVoice();
};
