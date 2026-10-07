#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include "DspCore.h"

namespace petrichor
{

/**
    Piano I's measured data: the Salamander Grand Piano V3 (a Yamaha C5 recorded by Alexander Holm,
    CC-BY 3.0) analysed by Tools/hybrid/analyse_salamander.py into the parameters of this engine's
    own modal voice. Every third key (A0, C1, D#1 .. C8) at 16 velocity layers:

      - per partial n: frequency ratio f_n / f_1, the prompt and aftersound amplitudes and decay
        rates, and the aftersound's beat (frequency offset) and phase - the two complex resonators
        PianoVoice gives each partial, fitted to the recording;
      - the attack residual: what is left of the first 250 ms when every partial is removed (hammer,
        action and soundboard noise), at four velocity layers, mu-law coded.

    So Piano I is resynthesised, not played back: the sound still comes from the voice's modes,
    which is what lets the wind bend them, the rain land in them and the thunder darken them.

    The tables are parsed from the embedded blob once, on first use. Call instance() from
    prepare(); lookups are noexcept and allocation-free.
*/
class PianoHybrid
{
public:
    static constexpr int kMaxPartials = 96;
    static constexpr int kLayers = 16;
    static constexpr int kResidualLayers = 4;

    static const PianoHybrid& instance();

    bool isValid() const noexcept { return valid; }
    int  getNumAnchors() const noexcept { return (int) anchors.size(); }

    /** Measured partials of one key at one velocity, interpolated between the sampled keys and layers. */
    struct Partials
    {
        int   count = 0;
        float ratio[kMaxPartials];  // f_n / f_1 (measured inharmonicity, including its irregularities)
        float amp[kMaxPartials];    // prompt amplitude; the key's mezzo-forte (V = 80) strike has unit energy
        float sigma1[kMaxPartials]; // prompt decay rate (1/s, amplitude; T60 = 6.91 / sigma)
        float sigma2[kMaxPartials]; // aftersound decay rate
        float after[kMaxPartials];  // aftersound amplitude / prompt amplitude
        float beatHz[kMaxPartials]; // aftersound frequency minus prompt frequency
        float phase[kMaxPartials];  // aftersound phase relative to the prompt (radians)
    };

    void partials (int midiKey, float midiVelocity, Partials& out) const noexcept;

    /** The attack residual for a key and velocity: two layers to crossfade, mu-law coded. */
    struct Residual
    {
        const std::uint8_t* data[2] { nullptr, nullptr };
        float gain[2] {};      // linear, in the units of Partials::amp
        int   length = 0;      // samples at sourceRate
        float sourceRate = 48000.0f;
        float sourceF0 = 0.0f; // measured fundamental of the sampled key (to re-pitch to the played key)
    };

    void residual (int midiKey, float midiVelocity, Residual& out) const noexcept;

    static float decode (std::uint8_t q) noexcept { return mulawTable()[q]; }

private:
    PianoHybrid();

    struct Anchor
    {
        int   key = 0, count = 0;
        float f0 = 0.0f, B = 0.0f, normDb = 0.0f;
        std::array<float, kMaxPartials> ratio {}, s1 {}, s2 {}, afterDb {}, beat {}, phase {};
        std::array<std::array<float, kMaxPartials>, kLayers> ampDb {};
        std::array<float, kResidualLayers> residualDb {};
        std::array<std::vector<std::uint8_t>, kResidualLayers> residual;
    };

    static void sanitise (Anchor& a) noexcept;
    float layerAmpDb (const Anchor& a, int n, float midiVelocity) const noexcept;
    static const std::array<float, 256>& mulawTable() noexcept;

    bool valid = false;
    int  residualLength = 0;
    float sampleRate = 48000.0f;
    std::array<float, kLayers> layerVelocity {};
    std::array<float, kResidualLayers> residualVelocity {};
    std::vector<Anchor> anchors;
};

} // namespace petrichor
