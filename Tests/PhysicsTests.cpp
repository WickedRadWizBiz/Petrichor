#include <iostream>
#include <cmath>
#include <cassert>
#include "../Source/DSP/RainEngine.h"
#include "../Source/DSP/WindModule.h"
#include "../Source/DSP/HygroEngine.h"

void testMinnaert()
{
    // Test Case 1: 1mm Drop (Radius 0.5mm)
    // f = 3.26 / 0.0005 = 6520 Hz
    float freq = RainEngine::getMinnaertFrequency(1.0f);
    std::cout << "Minnaert(1mm) = " << freq << " Hz (Expected ~6520)" << std::endl;
    assert(std::abs(freq - 6520.0f) < 10.0f);

    // Test Case 2: 5mm Drop (Radius 2.5mm)
    // f = 3.26 / 0.0025 = 1304 Hz
    freq = RainEngine::getMinnaertFrequency(5.0f);
    std::cout << "Minnaert(5mm) = " << freq << " Hz (Expected ~1304)" << std::endl;
    assert(std::abs(freq - 1304.0f) < 10.0f);
}

void testWind()
{
    WindModule wm;
    std::vector<WindModule::AeolianFilter> filters;

    // Test Case: 10 m/s Wind
    // Bass String D=0.005. St=0.2.
    // f = 0.2 * 10 / 0.005 = 400 Hz
    wm.update(10.0f, filters);

    std::cout << "Aeolian(Bass, 10m/s) = " << filters[0].frequency << " Hz (Expected 400)" << std::endl;
    assert(std::abs(filters[0].frequency - 400.0f) < 1.0f);
}

void testHygro()
{
    HygroEngine he;
    he.prepare(44100);

    // Simulate 1 second of 100% humidity
    for(int i=0; i<10; i++) // 10 steps of 0.1s
        he.process(1.0f, 0.1f);

    float pitchMod = he.getPitchMultiplier();
    std::cout << "Hygro Pitch Mod after 1s wet = " << pitchMod << " (Expected > 1.0)" << std::endl;
    assert(pitchMod > 1.0f);
}

int main()
{
    testMinnaert();
    testWind();
    testHygro();
    std::cout << "All Physics Tests Passed." << std::endl;
    return 0;
}
