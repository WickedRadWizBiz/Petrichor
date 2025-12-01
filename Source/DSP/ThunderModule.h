#pragma once

#include <juce_core/juce_core.h>

class ThunderModule
{
public:
    ThunderModule();

    // Call when Thunder button is pressed or triggered
    void triggerThunder(float distance0to1);

    // Process logic to return current Inharmonicity Multiplier
    // Ramps B up and down (N-wave shape)
    float getInharmonicityMultiplier(float timeDelta);

private:
    bool isActive = false;
    float currentInharmonicity = 1.0f;
    float peakInharmonicity = 100.0f;

    // Envelope state
    enum Stage { IDLE, ATTACK, HOLD, DECAY };
    Stage stage = IDLE;
    float timer = 0.0f;

    float attackTime = 0.005f; // Instant
    float holdTime = 0.05f;
    float decayTime = 5.0f; // Long tail
};
