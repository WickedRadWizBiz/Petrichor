#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

PetrichorAudioProcessor::PetrichorAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PETRICHOR", petrichor::params::createLayout())
{
    for (const auto& spec : petrichor::params::all())
        rawParams.push_back (apvts.getRawParameterValue (spec.id));
}

bool PetrichorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void PetrichorAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    keyboardState.reset();
}

double PetrichorAudioProcessor::getTailLengthSeconds() const
{
    // Longest string aftersound plus the far rumble zone.
    return 12.0 + (double) apvts.getRawParameterValue ("rumble_decay")->load();
}

void PetrichorAudioProcessor::handleMidi (const juce::MidiMessage& m) noexcept
{
    if (m.isNoteOn())
        engine.noteOn (m.getNoteNumber(), m.getVelocity());
    else if (m.isNoteOff())
        engine.noteOff (m.getNoteNumber());
    else if (m.isSustainPedalOn())
        engine.setSustainPedal (true);
    else if (m.isSustainPedalOff())
        engine.setSustainPedal (false);
    else if (m.isSoftPedalOn())
        engine.setSoftPedal (true);
    else if (m.isSoftPedalOff())
        engine.setSoftPedal (false);
    else if (m.isAllSoundOff())
        engine.allSoundOff();
    else if (m.isAllNotesOff() || m.isResetAllControllers())
        engine.allNotesOff();
}

void PetrichorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    petrichor::EngineParams p;
    const auto& specs = petrichor::params::all();
    for (size_t i = 0; i < specs.size(); ++i)
        p.*(specs[i].field) = rawParams[i]->load (std::memory_order_relaxed);
    engine.setParams (p);

    const int numChannels = buffer.getNumChannels();
    float* left = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    // Mono hosts: render the right channel into a small scratch buffer and fold down.
    float scratch[256];

    int position = 0;
    auto renderUpTo = [&] (int end)
    {
        while (position < end)
        {
            if (right != nullptr)
            {
                const int n = end - position;
                engine.process (left + position, right + position, n);
                position = end;
            }
            else
            {
                const int n = std::min (end - position, (int) std::size (scratch));
                engine.process (left + position, scratch, n);
                for (int i = 0; i < n; ++i)
                    left[position + i] = 0.5f * (left[position + i] + scratch[i]);
                position += n;
            }
        }
    };

    // Split the block at each MIDI timestamp so events are sample accurate.
    for (const auto metadata : midi)
    {
        renderUpTo (juce::jlimit (position, numSamples, metadata.samplePosition));
        handleMidi (metadata.getMessage());
    }
    renderUpTo (numSamples);

    for (int ch = 2; ch < numChannels; ++ch)
        buffer.clear (ch, 0, numSamples);

    publishTelemetry();
}

void PetrichorAudioProcessor::publishTelemetry() noexcept
{
    const auto& t = engine.getTelemetry();
    constexpr auto relaxed = std::memory_order_relaxed;
    telemetry.windSpeed.store (t.windSpeed, relaxed);
    telemetry.windGust.store (t.windGust, relaxed);
    telemetry.gustFactor.store (t.gustFactor, relaxed);
    telemetry.rainRate.store (t.rainRate, relaxed);
    telemetry.lambda.store (t.lambda, relaxed);
    telemetry.grainRate.store (t.grainRate, relaxed);
    telemetry.meanDropMM.store (t.meanDropMM, relaxed);
    telemetry.impactSpeed.store (t.impactSpeed, relaxed);
    telemetry.activeVoices.store (t.activeVoices, relaxed);
    telemetry.lastStrikeDistance.store (t.lastStrikeDistance, relaxed);
    telemetry.lastStrikeKey.store (t.lastStrikeKey, relaxed);
    telemetry.lastStrikeVelocity.store (t.lastStrikeVelocity, relaxed);
    for (size_t i = 0; i < t.keyLevels.size(); ++i)
        telemetry.keyLevels[i].store (t.keyLevels[i], relaxed);
    telemetry.strikeCount.store (t.strikeCount, std::memory_order_release);
}

juce::AudioProcessorEditor* PetrichorAudioProcessor::createEditor()
{
    return new PetrichorAudioProcessorEditor (*this);
}

void PetrichorAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PetrichorAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PetrichorAudioProcessor();
}
