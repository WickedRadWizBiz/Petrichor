#include "RainEngine.h"

RainEngine::RainEngine()
{
    std::random_device rd;
    rng.seed(rd());
}

RainEngine::~RainEngine()
{
}

void RainEngine::prepare(double sampleRate)
{
    fs = sampleRate;
}

void RainEngine::process(float rainIntensity, std::vector<RainDrop>& newDrops)
{
    // rainIntensity is 0.0 to 1.0 (normalized)
    float R = rainIntensity * 100.0f;

    if (R < 0.1f) return;

    // Lambda = 4.1 * R^(-0.21)
    float lambda = 4.1f * std::pow(R, -0.21f);

    float dropsPerSecond = 10.0f + (R * 5.0f);
    float dropsPerBlock = dropsPerSecond * (512.0f / fs);

    std::poisson_distribution<int> pDist(dropsPerBlock);
    int numDrops = pDist(rng);

    std::exponential_distribution<float> dDist(lambda);

    for (int i = 0; i < numDrops; ++i)
    {
        RainDrop drop;
        drop.diameterMM = dDist(rng);

        if (drop.diameterMM < 0.1f) drop.diameterMM = 0.1f;

        float freq = getMinnaertFrequency(drop.diameterMM);
        drop.midiNote = getMidiNoteFromFrequency(freq);

        float vel = jmap(drop.diameterMM, 0.1f, 5.0f, 10.0f, 127.0f);
        drop.velocity = jlimit(1.0f, 127.0f, vel) / 127.0f;

        drop.timeToImpact = 0.0f;

        newDrops.push_back(drop);
    }
}

float RainEngine::getMinnaertFrequency(float diameterMM)
{
    float a = (diameterMM / 2.0f) / 1000.0f;
    if (a <= 0.00001f) return 20000.0f;

    float f = 3.26f / a;
    return f;
}

int RainEngine::getMidiNoteFromFrequency(float freq)
{
    // MIDI Note = 69 + 12 * log2(freq / 440)
    return roundToInt(69.0 + 12.0 * std::log2(freq / 440.0));
}

float RainEngine::generateDropDiameter(float rainRateMMPerHr)
{
    return 0.0f;
}
