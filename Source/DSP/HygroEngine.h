#pragma once

#include <cmath>

class HygroEngine
{
public:
    HygroEngine();
    ~HygroEngine();

    void prepare(double sampleRate);

    float process(float targetHumidity, float timeDeltaSeconds);

    float getPitchMultiplier() const;
    float getDecayMultiplier() const;

private:
    float currentMoisture = 0.0f;
    float sorptionRate = 0.01f;
    float desorptionRate = 0.05f;
};
