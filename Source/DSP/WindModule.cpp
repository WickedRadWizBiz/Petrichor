#include "WindModule.h"

void WindModule::update(float windSpeed, std::vector<AeolianFilter>& filters)
{
    filters.clear();

    if (windSpeed < 0.1f) return;

    // Strouhal Number St = 0.2
    float st = 0.2f;

    // Bass
    float fBass = (st * windSpeed) / bassDiameter;
    // Tenor
    float fTenor = (st * windSpeed) / tenorDiameter;
    // Treble
    float fTreble = (st * windSpeed) / trebleDiameter;

    // Gain proportional to V^6 (Dipole) -> highly sensitive
    // We'll normalize for audio usage.
    float gain = std::pow(windSpeed, 2.0f) * 0.001f;

    filters.push_back({fBass, gain, 10.0f});
    filters.push_back({fTenor, gain, 20.0f});
    filters.push_back({fTreble, gain * 0.5f, 30.0f});
}
