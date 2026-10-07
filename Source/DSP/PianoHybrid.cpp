#include "PianoHybrid.h"

#include <cstring>

namespace petrichor
{

namespace data
{
    // Source/DSP/Data/piano_hybrid.bin, embedded at build time by Tools/BinToCpp.
    extern const std::uint32_t pianoHybridWords[];
    extern const std::size_t pianoHybridBytes;
}

namespace
{
    /** Little-endian reader over the embedded words (independent of the target's byte order). */
    struct BlobReader
    {
        std::size_t pos = 0;

        bool canRead (std::size_t n) const noexcept { return pos + n <= data::pianoHybridBytes; }

        std::uint8_t byte (std::size_t i) const noexcept
        {
            return (std::uint8_t) ((data::pianoHybridWords[i >> 2] >> (8u * (unsigned) (i & 3u))) & 0xFFu);
        }

        std::uint32_t u32() noexcept
        {
            std::uint32_t v = 0;
            for (unsigned b = 0; b < 4; ++b)
                v |= (std::uint32_t) byte (pos + b) << (8u * b);
            pos += 4;
            return v;
        }

        std::int32_t i32() noexcept { return (std::int32_t) u32(); }

        float f32() noexcept
        {
            const std::uint32_t v = u32();
            float f;
            std::memcpy (&f, &v, sizeof f);
            return f;
        }
    };

    constexpr float kReferenceVelocity = 80.0f; // the key's mezzo-forte strike has unit energy
}

//==============================================================================
const PianoHybrid& PianoHybrid::instance()
{
    static const PianoHybrid hybrid;
    return hybrid;
}

const std::array<float, 256>& PianoHybrid::mulawTable() noexcept
{
    static const std::array<float, 256> table = []
    {
        std::array<float, 256> t {};
        constexpr float mu = 255.0f;
        for (int q = 0; q < 256; ++q)
        {
            const float c = (float) q / 127.5f - 1.0f;
            const float y = (std::pow (1.0f + mu, std::abs (c)) - 1.0f) / mu;
            t[(size_t) q] = c < 0.0f ? -y : y;
        }
        return t;
    }();
    return table;
}

PianoHybrid::PianoHybrid()
{
    mulawTable(); // build it here, not on first use in the audio thread

    BlobReader r;
    if (! r.canRead (36))
        return;
    const char magic[4] = { (char) r.byte (0), (char) r.byte (1), (char) r.byte (2), (char) r.byte (3) };
    if (std::memcmp (magic, "PPHY", 4) != 0)
        return;
    r.pos = 4;

    const std::uint32_t version = r.u32();
    const int numAnchors = (int) r.u32();
    const int layers = (int) r.u32();
    const int maxPartials = (int) r.u32();
    sampleRate = (float) r.u32();
    const int resLayers = (int) r.u32();
    residualLength = (int) r.u32();
    if (version != 1 || layers != kLayers || maxPartials != kMaxPartials || resLayers != kResidualLayers
        || numAnchors < 2 || residualLength <= 0)
        return;

    for (auto& v : layerVelocity)
        v = r.f32();
    for (auto& v : residualVelocity)
        v = r.f32();

    anchors.resize ((size_t) numAnchors);
    for (auto& a : anchors)
    {
        if (! r.canRead (16))
            return;
        a.key = r.i32();
        a.count = std::clamp ((int) r.i32(), 0, kMaxPartials);
        a.f0 = r.f32();
        a.B = r.f32();
        if (! r.canRead ((std::size_t) a.count * (6 + kLayers) * 4))
            return;
        for (int n = 0; n < a.count; ++n)
        {
            a.ratio[(size_t) n]   = r.f32();
            a.s1[(size_t) n]      = r.f32();
            a.s2[(size_t) n]      = r.f32();
            a.afterDb[(size_t) n] = r.f32();
            a.beat[(size_t) n]    = r.f32();
            a.phase[(size_t) n]   = r.f32();
            for (int l = 0; l < kLayers; ++l)
                a.ampDb[(size_t) l][(size_t) n] = r.f32();
        }
        sanitise (a);
    }

    for (auto& a : anchors)
    {
        for (int l = 0; l < kResidualLayers; ++l)
        {
            if (! r.canRead (4 + (std::size_t) residualLength))
                return;
            a.residualDb[(size_t) l] = r.f32();
            a.residual[(size_t) l].resize ((size_t) residualLength);
            for (int i = 0; i < residualLength; ++i)
                a.residual[(size_t) l][(size_t) i] = r.byte (r.pos + (std::size_t) i);
            r.pos += (std::size_t) residualLength;
        }

        // Each key's mezzo-forte strike has unit energy (prompt plus aftersound of every partial):
        // the loudness across the keyboard is then calibrated like the rest of the engine.
        double energy = 0.0;
        for (int n = 0; n < a.count; ++n)
        {
            const double p = std::pow (10.0, layerAmpDb (a, n, kReferenceVelocity) / 10.0);
            energy += p * (1.0 + std::pow (10.0, a.afterDb[(size_t) n] / 10.0));
        }
        a.normDb = (float) (10.0 * std::log10 (std::max (energy, 1.0e-30)));
    }

    valid = true;
}

//==============================================================================
void PianoHybrid::sanitise (Anchor& a) noexcept
{
    // The voice must stay stable whatever the analysis produced: finite, decaying, in-range values,
    // and no partial far above the strongest of the low ones (a stray fit, not a sound).
    auto finiteOr = [] (float x, float fallback) { return std::isfinite (x) ? x : fallback; };
    for (int n = 0; n < a.count; ++n)
    {
        const size_t s = (size_t) n;
        const float harmonic = (float) (n + 1);
        a.ratio[s]   = clampf (finiteOr (a.ratio[s], harmonic), 0.9f * harmonic, 1.6f * harmonic);
        a.s1[s]      = clampf (finiteOr (a.s1[s], 1.0f), 0.05f, 200.0f);
        a.s2[s]      = clampf (finiteOr (a.s2[s], 0.2f), 0.05f, a.s1[s]);
        a.afterDb[s] = clampf (finiteOr (a.afterDb[s], -30.0f), -80.0f, 12.0f);
        a.beat[s]    = clampf (finiteOr (a.beat[s], 0.0f), -5.0f, 5.0f);
        a.phase[s]   = finiteOr (a.phase[s], 0.0f);
    }
    if (a.count > 0)
        a.ratio[0] = 1.0f;

    for (auto& row : a.ampDb)
    {
        float strongest = -160.0f;
        for (int n = 0; n < std::min (a.count, 12); ++n)
            strongest = std::max (strongest, finiteOr (row[(size_t) n], -160.0f));
        for (int n = 0; n < a.count; ++n)
            row[(size_t) n] = clampf (finiteOr (row[(size_t) n], -160.0f), -160.0f, strongest + 8.0f);
    }
}

float PianoHybrid::layerAmpDb (const Anchor& a, int n, float v) const noexcept
{
    const auto& lv = layerVelocity;
    if (v <= lv[0])
        return a.ampDb[0][(size_t) n] + 20.0f * std::log10 (std::max (v, 1.0f) / lv[0]); // ~ velocity below the softest layer
    if (v >= lv[kLayers - 1])
        return a.ampDb[kLayers - 1][(size_t) n];

    int j = 0;
    while (j < kLayers - 2 && v > lv[(size_t) j + 1])
        ++j;
    const float x = (v - lv[(size_t) j]) / std::max (lv[(size_t) j + 1] - lv[(size_t) j], 1.0e-3f);
    return lerpf (a.ampDb[(size_t) j][(size_t) n], a.ampDb[(size_t) j + 1][(size_t) n], x);
}

void PianoHybrid::partials (int midiKey, float midiVelocity, Partials& out) const noexcept
{
    out.count = 0;
    if (! valid)
        return;

    const int numAnchors = (int) anchors.size();
    const int k = std::clamp (midiKey, anchors.front().key, anchors.back().key);
    int i = 0;
    while (i < numAnchors - 2 && k >= anchors[(size_t) i + 1].key)
        ++i;
    const Anchor& A = anchors[(size_t) i];
    const Anchor& B = anchors[(size_t) i + 1];
    const float x = clampf ((float) (k - A.key) / (float) std::max (B.key - A.key, 1), 0.0f, 1.0f);
    const float v = clampf (midiVelocity, 1.0f, 127.0f);

    const int count = std::min (std::max (A.count, B.count), kMaxPartials);
    for (int n = 0; n < count; ++n)
    {
        const bool inA = n < A.count, inB = n < B.count;
        const size_t s = (size_t) n;
        float ampDb, ratio, ls1, ls2, afterDb, beat, phase;

        if (inA && inB)
        {
            ampDb   = lerpf (layerAmpDb (A, n, v) - A.normDb, layerAmpDb (B, n, v) - B.normDb, x);
            ratio   = lerpf (A.ratio[s], B.ratio[s], x);
            ls1     = lerpf (std::log (A.s1[s]), std::log (B.s1[s]), x);
            ls2     = lerpf (std::log (A.s2[s]), std::log (B.s2[s]), x);
            afterDb = lerpf (A.afterDb[s], B.afterDb[s], x);
            beat    = lerpf (A.beat[s], B.beat[s], x);
            phase   = x < 0.5f ? A.phase[s] : B.phase[s];
        }
        else
        {
            // Only one neighbour reaches this high: fade the partial out toward the other.
            const Anchor& P = inA ? A : B;
            const float away = inA ? x : 1.0f - x;
            ampDb   = layerAmpDb (P, n, v) - P.normDb - 20.0f * away;
            ratio   = P.ratio[s];
            ls1     = std::log (P.s1[s]);
            ls2     = std::log (P.s2[s]);
            afterDb = P.afterDb[s];
            beat    = P.beat[s];
            phase   = P.phase[s];
        }

        out.ratio[s]  = std::max (ratio, 0.5f * (float) (n + 1));
        out.amp[s]    = std::pow (10.0f, ampDb / 20.0f);
        out.sigma1[s] = std::exp (ls1);
        out.sigma2[s] = std::exp (ls2);
        out.after[s]  = std::pow (10.0f, afterDb / 20.0f);
        out.beatHz[s] = beat;
        out.phase[s]  = phase;
    }
    out.count = count;
}

void PianoHybrid::residual (int midiKey, float midiVelocity, Residual& out) const noexcept
{
    out = Residual{};
    if (! valid)
        return;

    // The nearest sampled key (re-pitched by the voice to the key played).
    const Anchor* nearest = &anchors.front();
    for (const auto& a : anchors)
        if (std::abs (a.key - midiKey) < std::abs (nearest->key - midiKey))
            nearest = &a;
    const Anchor& a = *nearest;

    out.length = residualLength;
    out.sourceRate = sampleRate;
    out.sourceF0 = a.f0;

    const auto& rv = residualVelocity;
    const float v = clampf (midiVelocity, 1.0f, 127.0f);
    auto level = [&a] (int layer) { return a.residualDb[(size_t) layer] - a.normDb; };

    if (v <= rv[0])
    {
        out.data[0] = out.data[1] = a.residual[0].data();
        out.gain[0] = std::pow (10.0f, level (0) / 20.0f) * (v / rv[0]);
        return;
    }
    if (v >= rv[kResidualLayers - 1])
    {
        out.data[0] = out.data[1] = a.residual[kResidualLayers - 1].data();
        out.gain[0] = std::pow (10.0f, level (kResidualLayers - 1) / 20.0f);
        return;
    }

    int j = 0;
    while (j < kResidualLayers - 2 && v > rv[(size_t) j + 1])
        ++j;
    const float y = (v - rv[(size_t) j]) / std::max (rv[(size_t) j + 1] - rv[(size_t) j], 1.0e-3f);
    // Two different noise recordings: crossfade at equal power, at the interpolated level.
    const float g = std::pow (10.0f, lerpf (level (j), level (j + 1), y) / 20.0f);
    out.data[0] = a.residual[(size_t) j].data();
    out.data[1] = a.residual[(size_t) j + 1].data();
    out.gain[0] = g * std::cos (0.5f * kPi * y);
    out.gain[1] = g * std::sin (0.5f * kPi * y);
}

} // namespace petrichor
