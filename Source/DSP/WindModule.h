#pragma once

#include <vector>
#include <cmath>

class WindModule
{
public:
    struct AeolianFilter
    {
        float frequency;
        float gain;
        float q;
    };

    /**
     * @brief Calculates active Aeolian frequencies based on wind speed.
     * f = 0.2 * V / D
     */
    void update(float windSpeedMps, std::vector<AeolianFilter>& filters);

private:
    // Wire diameters in meters
    const float bassDiameter = 5.0e-3f;
    const float tenorDiameter = 1.25e-3f;
    const float trebleDiameter = 0.79e-3f;
};
