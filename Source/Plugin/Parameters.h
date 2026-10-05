#pragma once

#include <JuceHeader.h>
#include "PetrichorEngine.h"

namespace petrichor::params
{

/** One automatable control. The frontend binds to `id` through a WebSliderRelay of the same name. */
struct Spec
{
    const char* id;
    const char* name;
    const char* unit;
    float min, max, defaultValue;
    float centre; // value shown at the knob's midpoint (skew); <= min means linear
    float EngineParams::* field;
};

// Keep in sync with frontend/src/params.js (ids, ranges and defaults).
inline const std::array<Spec, 22>& all()
{
    static const std::array<Spec, 22> specs { {
        // Piano
        { "hammer_hardness", "Hammer Hardness", "",     0.0f,    1.0f,    0.5f,   0.0f,   &EngineParams::hammerHardness },
        { "sustain",         "Sustain",         "x",    0.3f,    3.0f,    1.0f,   1.0f,   &EngineParams::sustain },
        { "unison",          "String Detune",   "ct",   0.0f,    4.0f,    1.2f,   0.0f,   &EngineParams::unisonCents },
        { "stereo_width",    "Stereo Width",    "",     0.0f,    1.0f,    0.7f,   0.0f,   &EngineParams::stereoWidth },
        { "piano_level",     "Piano Level",     "dB",  -24.0f,   6.0f,    0.0f,   0.0f,   &EngineParams::pianoLevelDb },
        { "tuning",          "Tuning A4",       "Hz",  415.0f, 466.0f,  440.0f,   0.0f,   &EngineParams::tuningA4Hz },

        // Thunder
        { "storm_distance",  "Storm Distance",  "m",    50.0f, 4000.0f, 1200.0f, 800.0f, &EngineParams::stormDistanceM },
        { "crack_level",     "Crack",           "",      0.0f,    1.0f,    0.5f,   0.0f,   &EngineParams::crackLevel },
        { "air_absorption",  "Air Absorption",  "",      0.0f,    1.0f,    0.4f,   0.0f,   &EngineParams::airAbsorption },
        { "rumble_mix",      "Rumble",          "",      0.0f,    1.0f,    0.35f,  0.0f,   &EngineParams::rumbleMix },
        { "rumble_decay",    "Rumble Decay",    "s",     0.5f,   12.0f,    5.0f,   4.0f,   &EngineParams::rumbleDecayS },

        // Wind
        { "wind_speed",      "Wind Speed",      "m/s",   0.0f,   30.0f,    8.0f,   8.0f,   &EngineParams::windSpeedMs },
        { "turbulence",      "Turbulence",      "",      0.0f,    1.0f,    0.35f,  0.0f,   &EngineParams::turbulence },
        { "gust_length",     "Gust Length",     "m",     2.0f,  100.0f,   12.0f,  15.0f,   &EngineParams::gustLengthM },
        { "wind_drift",      "Doppler Drift",   "",      0.0f,    1.0f,    0.35f,  0.0f,   &EngineParams::windDrift },
        { "wind_filter",     "Gust Filter",     "",      0.0f,    1.0f,    0.4f,   0.0f,   &EngineParams::windFilter },
        { "wind_air",        "Wind Air",        "",      0.0f,    1.0f,    0.15f,  0.0f,   &EngineParams::windAir },

        // Rain
        { "rain_rate",       "Rain Rate",       "mm/h",  0.0f,  150.0f,    8.0f,  15.0f,   &EngineParams::rainRateMMh },
        { "rain_coupling",   "Wind Coupling",   "",      0.0f,    1.0f,    0.6f,   0.0f,   &EngineParams::rainCoupling },
        { "rain_level",      "Rain Level",      "",      0.0f,    1.0f,    0.35f,  0.0f,   &EngineParams::rainLevel },
        { "rain_surface",    "Puddles",         "",      0.0f,    1.0f,    0.3f,   0.0f,   &EngineParams::rainSurface },

        // Master
        { "master",          "Master",          "dB",  -24.0f,   6.0f,   -2.0f,   0.0f,   &EngineParams::masterDb },
    } };
    return specs;
}

inline juce::NormalisableRange<float> rangeFor (const Spec& s)
{
    juce::NormalisableRange<float> range (s.min, s.max);
    if (s.centre > s.min && s.centre < s.max)
        range.setSkewForCentre (s.centre);
    return range;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& s : all())
    {
        const juce::String unit (s.unit);
        auto toText = [unit, s] (float v, int)
        {
            const bool percent = unit.isEmpty();
            if (percent)
                return juce::String (juce::roundToInt (v * 100.0f)) + " %";
            const int decimals = (s.max - s.min) <= 5.0f ? 2 : ((s.max - s.min) <= 50.0f ? 1 : 0);
            return juce::String (v, decimals) + " " + unit;
        };
        auto fromText = [unit] (const juce::String& text)
        {
            const float v = text.retainCharacters ("-0123456789.").getFloatValue();
            return unit.isEmpty() ? v / 100.0f : v;
        };

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { s.id, 1 }, s.name, rangeFor (s), s.defaultValue,
            juce::AudioParameterFloatAttributes().withLabel (unit)
                                                 .withStringFromValueFunction (toText)
                                                 .withValueFromStringFunction (fromText)));
    }
    return layout;
}

} // namespace petrichor::params
