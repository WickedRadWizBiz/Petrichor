#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

/**
    The editor is a single WebBrowserComponent hosting the bundled React UI.

    Frontend <-> backend contract (see frontend/src/juce.js):
      - every parameter is a WebSliderRelay named by its parameter id (Parameters.h)
      - native functions  noteOn(key, velocity), noteOff(key)  play the on-screen keyboard
      - event "telemetry" (~30 Hz) carries the storm state for the visualiser
*/
class PetrichorAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      private juce::Timer
{
public:
    explicit PetrichorAudioProcessorEditor (PetrichorAudioProcessor&);
    ~PetrichorAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    juce::WebBrowserComponent::Options createWebOptions();
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url) const;

    PetrichorAudioProcessor& processorRef;

    // Declaration order matters: relays must exist before the browser is built from them and
    // must outlive it; attachments are created last and destroyed first.
    std::vector<std::unique_ptr<juce::WebSliderRelay>> relays;
    juce::WebBrowserComponent webView;
    std::vector<std::unique_ptr<juce::WebSliderParameterAttachment>> attachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PetrichorAudioProcessorEditor)
};
