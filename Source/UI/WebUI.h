#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "../PluginProcessor.h"

// ==============================================================================
class WebUI : public juce::WebBrowserComponent,
              public juce::AudioProcessorValueTreeState::Listener
{
public:
    WebUI(PetrichorAudioProcessor& p);
    ~WebUI();

    // Resource provider for serving local content
    bool pageAboutToLoad(const juce::String& newURL) override;

    // Parameter Listener
    void parameterChanged(const juce::String& parameterID, float newValue) override;

private:
    PetrichorAudioProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WebUI)
};
