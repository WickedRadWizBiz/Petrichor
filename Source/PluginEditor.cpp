#include "PluginProcessor.h"
#include "PluginEditor.h"

PetrichorAudioProcessorEditor::PetrichorAudioProcessorEditor (PetrichorAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), webUI(p)
{
    addAndMakeVisible(webUI);
    setSize (1000, 600);
    setResizable(true, true);
}

PetrichorAudioProcessorEditor::~PetrichorAudioProcessorEditor()
{
}

void PetrichorAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void PetrichorAudioProcessorEditor::resized()
{
    webUI.setBounds(getLocalBounds());
}
