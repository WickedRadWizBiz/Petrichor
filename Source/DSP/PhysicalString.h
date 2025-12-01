#pragma once

#include <JuceHeader.h>
#include <vector>
#include <cmath>

/**
 * @brief PhysicalString - A Digital Waveguide Model of a stiff string.
 *
 * Implements:
 * 1. Dual Delay Lines (Right/Left)
 * 2. Dispersion Filter (Thiran Allpass) for stiffness ($B$)
 * 3. Longitudinal Wave output (Phantom Partials)
 * 4. Nonlinear Excitation logic (Hunt-Crossley)
 */
class PhysicalString
{
public:
    PhysicalString();
    ~PhysicalString();

    void prepare(double sampleRate);
    void reset();

    /**
     * @brief Configures the string physical properties.
     * @param frequency Fundamental frequency (Hz)
     * @param stiffnessCoefficient Inharmonicity coefficient (B). Range: 0.0001 (Treble) to 0.01 (Bass)
     * @param decayTime T60 decay time in seconds
     */
    void setParameters(float frequency, float stiffnessCoefficient, float decayTime);

    /**
     * @brief Excites the string with an input sample (e.g., hammer or rain impulse).
     * @param input The excitation signal (force/displacement)
     * @return The output sample from the bridge (transverse wave)
     */
    float process(float input);

    /**
     * @brief Gets the current output of the longitudinal mode.
     * Use this to mix in "phantom partials" or Aeolian tones.
     */
    float getLongitudinalOutput() const;

    /**
     * @brief Directly excite the longitudinal mode (e.g., for Wind/Aeolian effects).
     */
    void exciteLongitudinal(float input);

    // Setters for realtime modulation (Thunder/Humidity)
    void setStiffness(float newB);
    void setTensionModulation(float multiplier); // Simulates humidity swelling (pitch shift)

private:
    double fs = 44100.0;
    float frequency = 440.0f;
    float B = 0.0001f; // Stiffness
    float T60 = 1.0f;

    // Waveguide State
    std::vector<float> delayLine;
    int writeIndex = 0;
    int readIndex = 0; // Fractional read index handled by interpolation
    float delayLength = 100.0f;

    // Dispersion Filter (First order Allpass for simplicity, or Thiran)
    // y[n] = a * x[n] + x[n-1] - a * y[n-1]
    float apInput = 0.0f;
    float apOutput = 0.0f;
    float apCoeff = 0.0f;

    // Lowpass Filter for loss (Bridge reflection)
    float lpState = 0.0f;
    float lpCoeff = 0.99f; // Derived from T60

    // Longitudinal Mode (Simplified as a secondary resonator/delay)
    float longState = 0.0f;
    float longOutput = 0.0f;

    void updateCoefficients();
};
