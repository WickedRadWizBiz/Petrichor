#pragma once

#include <vector>
#include <random>
#include <cmath>

// Forward declare or include minimal headers if needed.
// Avoiding JuceHeader.h allows easier unit testing.

struct RainDrop
{
    float diameterMM;
    float velocity; // MIDI velocity 0-1
    int midiNote;
    float timeToImpact; // scheduling
};

class RainEngine
{
public:
    RainEngine();
    ~RainEngine();

    void prepare(double sampleRate);

    // Called every block to generate new drops based on intensity
    void process(float rainIntensity, std::vector<RainDrop>& newDrops);

    /**
     * @brief Minnaert Resonance Mapping
     * @param diameterMM Raindrop diameter in mm
     * @return Frequency in Hz
     */
    static float getMinnaertFrequency(float diameterMM);

    /**
     * @brief Maps Minnaert frequency to nearest MIDI note
     */
    static int getMidiNoteFromFrequency(float freq);

private:
    double fs = 44100.0;
    std::mt19937 rng;
    // std::exponential_distribution<float> poissonDist; // Unused

    float generateDropDiameter(float rainRateMMPerHr);

    // Helper helpers
    static float jmap(float val, float minIn, float maxIn, float minOut, float maxOut) {
        return minOut + (maxOut - minOut) * (val - minIn) / (maxIn - minIn);
    }
    static float jlimit(float min, float max, float val) {
        if(val < min) return min;
        if(val > max) return max;
        return val;
    }
    static int roundToInt(double val) { return (int)std::round(val); }
};
