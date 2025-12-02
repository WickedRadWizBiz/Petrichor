#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "UI/WebUI.h"

class PetrichorAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    PetrichorAudioProcessorEditor (PetrichorAudioProcessor&);
    ~PetrichorAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    PetrichorAudioProcessor& audioProcessor;
    WebUI webUI;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PetrichorAudioProcessorEditor)
};
