#include "ThunderModule.h"

ThunderModule::ThunderModule() {}

void ThunderModule::triggerThunder(float distance)
{
    // Distance modulates decay and peak intensity?
    // Near = Sharp/High B. Far = Low rumble.
    // For now, fixed.
    isActive = true;
    stage = ATTACK;
    timer = 0.0f;
    currentInharmonicity = 1.0f;
    // Reset or restart
}

float ThunderModule::getInharmonicityMultiplier(float dt)
{
    if (!isActive) return 1.0f;

    timer += dt;

    switch (stage)
    {
    case ATTACK:
        if (timer >= attackTime)
        {
            currentInharmonicity = peakInharmonicity;
            stage = HOLD;
            timer = 0.0f;
        }
        else
        {
            // Linear ramp 1 -> 100
            float alpha = timer / attackTime;
            currentInharmonicity = 1.0f + alpha * (peakInharmonicity - 1.0f);
        }
        break;

    case HOLD:
        if (timer >= holdTime)
        {
            stage = DECAY;
            timer = 0.0f;
        }
        break;

    case DECAY:
        if (timer >= decayTime)
        {
            currentInharmonicity = 1.0f;
            stage = IDLE;
            isActive = false;
        }
        else
        {
            // Exponential or Linear decay back to 1
            float alpha = 1.0f - (timer / decayTime);
            currentInharmonicity = 1.0f + alpha * (peakInharmonicity - 1.0f);
        }
        break;

    case IDLE:
        currentInharmonicity = 1.0f;
        break;
    }

    return currentInharmonicity;
}
