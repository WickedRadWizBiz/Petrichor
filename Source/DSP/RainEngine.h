#pragma once

#include <vector>
#include <random>
#include <cmath>

struct RainDrop
{
    float diameterMM;
    float velocity;
    int midiNote;
    float timeToImpact;
};

class RainEngine
{
public:
    RainEngine();
    ~RainEngine();

    void prepare(double sampleRate);

    // Updated signature: pass number of samples in block to calculate correct density
    void process(float rainIntensity, std::vector<RainDrop>& newDrops, int numSamples);

    static float getMinnaertFrequency(float diameterMM);
    static int getMidiNoteFromFrequency(float freq);

private:
    double fs = 44100.0;
    std::mt19937 rng;

    // Helpers
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
