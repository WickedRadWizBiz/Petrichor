#include "PluginProcessor.h"
#include "PluginEditor.h"

PetrichorAudioProcessor::PetrichorAudioProcessor()
     : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
       apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    rainIntensityParam = apvts.getRawParameterValue("rain_intensity");
    humidityParam = apvts.getRawParameterValue("humidity");
    windSpeedParam = apvts.getRawParameterValue("wind_speed");
    thunderDistParam = apvts.getRawParameterValue("thunder_distance");
    hammerHardnessParam = apvts.getRawParameterValue("hammer_hardness");
}

PetrichorAudioProcessor::~PetrichorAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout PetrichorAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>("rain_intensity", "Rain Intensity", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("humidity", "Humidity", 0.0f, 1.0f, 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("wind_speed", "Wind Speed", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("thunder_distance", "Thunder Distance", 0.0f, 1.0f, 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("hammer_hardness", "Hammer Hardness", 0.0f, 1.0f, 0.6f));

    return { params.begin(), params.end() };
}

void PetrichorAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    rainEngine.prepare(sampleRate);
    hygroEngine.prepare(sampleRate);
    voiceManager.prepare(sampleRate);
}

void PetrichorAudioProcessor::releaseResources()
{
}

void PetrichorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // 1. Update Physics Mods
    float timeDelta = buffer.getNumSamples() / getSampleRate();

    // Hygro
    float targetHumidity = *humidityParam;
    float currentMoisture = hygroEngine.process(targetHumidity, timeDelta);
    float pitchMod = hygroEngine.getPitchMultiplier();
    float decayMod = hygroEngine.getDecayMultiplier();

    // Thunder
    float inharmonicityMod = thunderModule.getInharmonicityMultiplier(timeDelta);

    // Wind
    float windSpd = *windSpeedParam * 35.0f; // 0-1 -> 0-35m/s (Hurricane)
    std::vector<float> windGains;
    std::vector<float> windFreqs;
    windModule.update(windSpd, windFilters);
    // Extract for voice manager (simplified)
    for(auto& f : windFilters) {
        windGains.push_back(f.gain);
        windFreqs.push_back(f.frequency);
    }

    // 2. Generate Rain
    float rainInt = *rainIntensityParam;
    dropQueue.clear();
    rainEngine.process(rainInt, dropQueue);

    // Trigger drops
    float hammerHard = *hammerHardnessParam;
    for (auto& drop : dropQueue)
    {
        voiceManager.noteOn(drop.midiNote, drop.velocity, hammerHard);
    }

    // Handle MIDI input (Human playing)
    for (const auto metadata : midiMessages)
    {
        auto msg = metadata.getMessage();
        if (msg.isNoteOn())
        {
            voiceManager.noteOn(msg.getNoteNumber(), msg.getFloatVelocity(), hammerHard);
        }
    }

    // 3. Process Voices
    voiceManager.process(buffer, inharmonicityMod, decayMod, pitchMod, windGains, windFreqs);
}

void PetrichorAudioProcessor::triggerThunder()
{
    thunderModule.triggerThunder(*thunderDistParam);

    // Also trigger bass notes for the effect
    // A0, A#0, B0
    voiceManager.noteOn(21, 1.0f, 1.0f);
    voiceManager.noteOn(22, 1.0f, 1.0f);
    voiceManager.noteOn(23, 1.0f, 1.0f);
}

juce::AudioProcessorEditor* PetrichorAudioProcessor::createEditor()
{
    return new PetrichorAudioProcessorEditor (*this);
}

bool PetrichorAudioProcessor::hasEditor() const { return true; }
const juce::String PetrichorAudioProcessor::getName() const { return "Petrichor"; }
bool PetrichorAudioProcessor::acceptsMidi() const { return true; }
bool PetrichorAudioProcessor::producesMidi() const { return false; }
bool PetrichorAudioProcessor::isMidiEffect() const { return false; }
double PetrichorAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int PetrichorAudioProcessor::getNumPrograms() { return 1; }
int PetrichorAudioProcessor::getCurrentProgram() { return 0; }
void PetrichorAudioProcessor::setCurrentProgram (int index) {}
const juce::String PetrichorAudioProcessor::getProgramName (int index) { return {}; }
void PetrichorAudioProcessor::changeProgramName (int index, const juce::String& newName) {}
void PetrichorAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}
void PetrichorAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PetrichorAudioProcessor();
}
