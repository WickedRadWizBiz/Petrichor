#include "PhysicalString.h"

PhysicalString::PhysicalString() {}
PhysicalString::~PhysicalString() {}

void PhysicalString::prepare(double sampleRate)
{
    fs = sampleRate;
    delayLine.resize(4096, 0.0f);
    reset();
}

void PhysicalString::reset()
{
    std::fill(delayLine.begin(), delayLine.end(), 0.0f);
    writeIndex = 0;
    lpState = 0.0f;
    apInput = 0.0f;
    apOutput = 0.0f;
    longState = 0.0f;
}

void PhysicalString::setParameters(float freq, float stiffnessCoefficient, float decayTime)
{
    this->frequency = freq;
    this->B = stiffnessCoefficient;
    this->T60 = decayTime;
    updateCoefficients();
}

void PhysicalString::setStiffness(float newB)
{
    if (this->B != newB) {
        this->B = newB;
        updateCoefficients();
    }
}

void PhysicalString::setTensionModulation(float multiplier)
{
    // Simplified pitch bend
    // frequency *= multiplier; // This would drift if called repeatedly
    // Just re-calc delay length for now? Too expensive per sample.
}

void PhysicalString::updateCoefficients()
{
    if (frequency < 20.0f) frequency = 20.0f;
    if (frequency > fs / 2.0f) frequency = fs / 2.0f;

    float periodSamples = fs / frequency;
    delayLength = periodSamples;

    // Map B to Allpass coeff (Dispersion)
    // 0 -> 0, 0.1 -> 0.7
    apCoeff = juce::jmap(B, 0.0f, 0.1f, 0.0f, 0.7f);

    float samplesPerDecay = T60 * fs;
    if (samplesPerDecay < 1.0f) samplesPerDecay = 1.0f;
    lpCoeff = std::pow(0.001f, 1.0f / samplesPerDecay);
}

float PhysicalString::process(float input)
{
    int iPart = (int)delayLength;
    float fPart = delayLength - iPart;

    int idxA = writeIndex - iPart;
    if (idxA < 0) idxA += delayLine.size();
    int idxB = idxA - 1;
    if (idxB < 0) idxB += delayLine.size();

    float sampleA = delayLine[idxA];
    float sampleB = delayLine[idxB];

    float delayedSample = sampleA + fPart * (sampleB - sampleA);

    // Allpass Dispersion
    float apIn = delayedSample;
    float apOut = apCoeff * apIn + apInput - apCoeff * apOutput;
    apInput = apIn;
    apOutput = apOut;

    // Loss
    float filtered = apOut * lpCoeff;
    lpState = (filtered * 0.5f) + (lpState * 0.5f);
    float fedBack = lpState;

    float output = fedBack + input;

    // Soft clip
    if (output > 2.0f) output = 2.0f;
    if (output < -2.0f) output = -2.0f;

    delayLine[writeIndex] = output;

    writeIndex++;
    if (writeIndex >= delayLine.size()) writeIndex = 0;

    longOutput = output * output;
    return output;
}

float PhysicalString::getLongitudinalOutput() const
{
    return longOutput;
}

void PhysicalString::exciteLongitudinal(float input)
{
    // Inject wind energy into the loop
    delayLine[writeIndex] += input * 0.1f;
}
