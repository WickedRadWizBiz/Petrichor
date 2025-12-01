#pragma once

#include <JuceHeader.h>
#include <vector>
#include <cmath>

/**
 * @brief PhysicalString - A Digital Waveguide Model of a stiff string.
 */
class PhysicalString
{
public:
    PhysicalString();
    ~PhysicalString();

    void prepare(double sampleRate);
    void reset();

    void setParameters(float frequency, float stiffnessCoefficient, float decayTime);

    /**
     * @brief Excites the string with an input sample.
     */
    float process(float input);

    float getLongitudinalOutput() const;
    void exciteLongitudinal(float input);

    // Setters for realtime modulation
    void setStiffness(float newB);
    void setTensionModulation(float multiplier);

    // Getter for base B to allow modulation scaling
    float getBaseStiffness() const { return B; }

private:
    double fs = 44100.0;
    float frequency = 440.0f;
    float B = 0.0001f;
    float T60 = 1.0f;

    std::vector<float> delayLine;
    int writeIndex = 0;
    float delayLength = 100.0f;

    float apInput = 0.0f;
    float apOutput = 0.0f;
    float apCoeff = 0.0f;

    float lpState = 0.0f;
    float lpCoeff = 0.99f;

    float longOutput = 0.0f;

    void updateCoefficients();
};
