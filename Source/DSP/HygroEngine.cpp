#include "HygroEngine.h"
#include <cmath>

HygroEngine::HygroEngine()
{
}

HygroEngine::~HygroEngine()
{
}

void HygroEngine::prepare(double sampleRate)
{
    sorptionRate = 0.1f;
    desorptionRate = 0.2f;
}

float HygroEngine::process(float targetHumidity, float timeDeltaSeconds)
{
    // Hysteresis / Lag
    if (targetHumidity > currentMoisture)
    {
        // Absorb (Wet) - Slower
        currentMoisture += (targetHumidity - currentMoisture) * sorptionRate * timeDeltaSeconds;
    }
    else
    {
        // Desorb (Dry) - Faster
        currentMoisture += (targetHumidity - currentMoisture) * desorptionRate * timeDeltaSeconds;
    }

    return currentMoisture;
}

float HygroEngine::getPitchMultiplier() const
{
    // Range: 1.0 (Dry) to 1.02 (Wet/Sharp)
    return 1.0f + (currentMoisture * 0.02f);
}

float HygroEngine::getDecayMultiplier() const
{
    // Range: 1.0 (Dry) to 0.4 (Wet/Dead).
    return 1.0f - (currentMoisture * 0.6f);
}
