#pragma once

#include <JuceHeader.h>
#include "PetrichorEngine.h"

/** Lock-free snapshot of the engine's telemetry for the editor (written on the audio thread). */
struct TelemetrySnapshot
{
    std::atomic<float> windSpeed { 0.0f }, windGust { 0.0f }, gustFactor { 1.0f };
    std::atomic<float> rainRate { 0.0f }, lambda { 0.0f }, grainRate { 0.0f }, meanDropMM { 0.0f }, impactSpeed { 0.0f };
    std::atomic<int> activeVoices { 0 };
    std::atomic<uint32_t> strikeCount { 0 };
    std::atomic<float> lastStrikeDistance { 0.0f }, lastStrikeKey { 0.0f }, lastStrikeVelocity { 0.0f };
    std::array<std::atomic<float>, 88> keyLevels {};
};

class PetrichorAudioProcessor : public juce::AudioProcessor
{
public:
    PetrichorAudioProcessor();
    ~PetrichorAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    /** Notes played on the editor's on-screen keyboard are merged into the MIDI stream here. */
    juce::MidiKeyboardState keyboardState;

    const TelemetrySnapshot& getTelemetry() const noexcept { return telemetry; }

private:
    void handleMidi (const juce::MidiMessage& m) noexcept;
    void publishTelemetry() noexcept;

    petrichor::PetrichorEngine engine;
    std::vector<std::atomic<float>*> rawParams;
    TelemetrySnapshot telemetry;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PetrichorAudioProcessor)
};
