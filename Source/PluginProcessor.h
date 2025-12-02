#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/RainEngine.h"
#include "DSP/PolyphonicVoiceManager.h"
#include "DSP/HygroEngine.h"
#include "DSP/ThunderModule.h"
#include "DSP/WindModule.h"

class PetrichorAudioProcessor  : public juce::AudioProcessor
{
public:
    PetrichorAudioProcessor();
    ~PetrichorAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Parameters
    std::atomic<float>* rainIntensityParam = nullptr;
    std::atomic<float>* humidityParam = nullptr;
    std::atomic<float>* windSpeedParam = nullptr;
    std::atomic<float>* thunderDistParam = nullptr;
    std::atomic<float>* hammerHardnessParam = nullptr;

    // Internal triggering
    void triggerThunder();

    juce::AudioProcessorValueTreeState apvts;

private:
    RainEngine rainEngine;
    HygroEngine hygroEngine;
    ThunderModule thunderModule;
    WindModule windModule;
    PolyphonicVoiceManager voiceManager;

    std::vector<RainDrop> dropQueue;
    std::vector<WindModule::AeolianFilter> windFilters;

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PetrichorAudioProcessor)
};
