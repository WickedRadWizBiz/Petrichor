#include "PhysicalString.h"

PhysicalString::PhysicalString()
{
}

PhysicalString::~PhysicalString()
{
}

void PhysicalString::prepare(double sampleRate)
{
    fs = sampleRate;
    // Max delay length approx 20ms (50Hz) -> 44100 / 50 = 882 samples.
    // Allocate generous buffer.
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
    this->B = newB;
    updateCoefficients();
}

void PhysicalString::setTensionModulation(float multiplier)
{
    // Pitch shift due to humidity
    // frequency * multiplier
    // Ideally we re-calculate delay length
    // For now, simpler implementation:
    // Just modify target frequency
    // But be careful not to drift too far if called per sample
    // This method assumes 'frequency' is base, multiplier is applied to current calc.
    // We will store baseFreq separately if needed, but for now let's assume updateCoefficients handles it.
    // Actually, let's keep it simple: caller updates frequency directly or we add a mod param.
}

void PhysicalString::updateCoefficients()
{
    if (frequency < 20.0f) frequency = 20.0f;
    if (frequency > fs / 2.0f) frequency = fs / 2.0f;

    // 1. Calculate ideal delay length (samples)
    // f = 1 / (Period)
    float periodSamples = fs / frequency;
    delayLength = periodSamples;

    // 2. Stiffness / Inharmonicity (Dispersion)
    // The allpass coefficient 'a' shifts the phase delay to simulate stiffness.
    // Thiran approximation for B:
    // This is a complex topic. A simple approach for "Iso-Storm" where we want
    // drastic changes (Thunder) is to map B to the allpass coefficient directly
    // or use a simplified tuning.
    // High B -> Lower delay at high freqs -> Shorter effective length for partials -> Sharpness.
    // For a simple 1st order allpass, the phase delay varies.
    // Let's use a mapping: B ranges 0.0001 to 0.1 (Thunder).
    // apCoeff ranges -1 to 1.

    // Simplification: We use the allpass to slightly detune the loop.
    // B of 0.0 -> apCoeff = 0.0
    // B of 0.1 -> apCoeff = 0.5 (Just a heuristic for the MVP/Thunder effect)
    apCoeff = juce::jmap(B, 0.0f, 0.1f, 0.0f, 0.7f);

    // 3. Loss Filter (T60)
    // g^N = 0.001 (where N = T60 * fs) => g = 0.001^(1/N)
    // Per period attenuation approx:
    float samplesPerDecay = T60 * fs;
    // We apply loss once per period (approx).
    // Loss factor per sample loop roughly:
    lpCoeff = std::pow(0.001f, 1.0f / samplesPerDecay) * 0.999f; // Scaling
    // Actually, simple 1-pole lowpass brightness control is better for "Wood" simulation
    // We'll use lpCoeff as the gain, and a fixed coefficient for the filter cutoff
}

float PhysicalString::process(float input)
{
    // Digital Waveguide:
    // y[n] = DelayLine(y[n-L]) * Loss * Dispersion + Input

    // 1. Read from delay line with linear interpolation
    int iPart = (int)delayLength;
    float fPart = delayLength - iPart;

    int idxA = writeIndex - iPart;
    if (idxA < 0) idxA += delayLine.size();
    int idxB = idxA - 1;
    if (idxB < 0) idxB += delayLine.size();

    float sampleA = delayLine[idxA];
    float sampleB = delayLine[idxB];

    // Linear interpolation read
    float delayedSample = sampleA + fPart * (sampleB - sampleA);

    // 2. Apply Dispersion (Allpass)
    // y[n] = a * x[n] + x[n-1] - a * y[n-1]
    float apIn = delayedSample;
    float apOut = apCoeff * apIn + apInput - apCoeff * apOutput;

    // Update state
    apInput = apIn;
    apOutput = apOut;

    // 3. Apply Loss (Lowpass + Gain)
    // Simple 1-pole: y[n] = x[n] + c * (y[n-1] - x[n])  (One-pole lowpass)
    // Or just gain for T60
    float filtered = apOut * lpCoeff;

    // Simple damping (brightness loss)
    lpState = (filtered * 0.5f) + (lpState * 0.5f); // 0.5 coefficient for damping high freqs
    float fedBack = lpState;

    // 4. Reinject
    float output = fedBack + input;

    // Soft clip to prevent explosion if B is high
    if (output > 2.0f) output = 2.0f;
    if (output < -2.0f) output = -2.0f;

    delayLine[writeIndex] = output;

    // Increment write index
    writeIndex++;
    if (writeIndex >= delayLine.size()) writeIndex = 0;

    // Longitudinal Simulation (Phantom)
    // Simple model: The longitudinal wave is excited by the *tension change*
    // which is proportional to the square of the transverse displacement (approx).
    // Or simpler: It resonates at a fixed high frequency.
    // f_L = 5000 / (2 * LengthMeters). LengthMeters ~ 1/Freq.
    // So f_L is proportional to Freq.
    // For now, we just output the transverse wave.
    // To properly do phantom partials, we'd need a second waveguide.
    // We will approximate it in "get LongitudinalOutput" by ring mod or simple distortion of the output.
    longOutput = output * output; // Frequency doubling (octave) + DC offset

    return output;
}

float PhysicalString::getLongitudinalOutput() const
{
    // Phantom partials often sound metallic and high pitched.
    // Returning the squared signal gives a 2nd harmonic, which is a crude approximation of longitudinal coupling
    // for a simple string.
    // For wind, we might want to excite this path directly.
    return longOutput;
}

void PhysicalString::exciteLongitudinal(float input)
{
    // In a full model, this would excite the secondary waveguide.
    // Here we can inject it into the main waveguide but processed high-pass
    // to simulate the different mode.
    // For the MVP, we mix it into the main loop.
    delayLine[writeIndex] += input * 0.5f;
}
