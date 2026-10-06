// Physics and behaviour tests for the Petrichor engine. No framework, no JUCE: build and run
//   cmake -B build -DPETRICHOR_ENGINE_ONLY=ON && cmake --build build && ./build/PetrichorTests

#define _USE_MATH_DEFINES // M_PI on MSVC
#include <chrono>
#include <complex>
#include <cstdio>
#include <functional>
#include <vector>

#include "../Source/DSP/PetrichorEngine.h"

using namespace petrichor;

namespace
{
int failures = 0, checks = 0;

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        ++checks;                                                           \
        if (! (cond)) {                                                     \
            ++failures;                                                     \
            std::printf ("  FAIL %s:%d  %s  -- ", __FILE__, __LINE__, #cond); \
            std::printf (__VA_ARGS__);                                      \
            std::printf ("\n");                                             \
        }                                                                   \
    } while (0)

constexpr double kFs = 48000.0;

//==============================================================================
void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * M_PI / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

/** Hann-windowed magnitude spectrum of x[start, start + n). */
std::vector<double> magnitudeSpectrum (const std::vector<float>& x, size_t start, size_t n)
{
    std::vector<std::complex<double>> a (n);
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * (double) i / (double) n);
        a[i] = (start + i < x.size() ? x[start + i] : 0.0f) * w;
    }
    fft (a);
    std::vector<double> m (n / 2);
    for (size_t i = 0; i < n / 2; ++i) m[i] = std::abs (a[i]);
    return m;
}

double centroidHz (const std::vector<double>& mag, double fs, size_t n)
{
    double num = 0.0, den = 0.0;
    for (size_t i = 1; i < mag.size(); ++i)
    {
        const double p = mag[i] * mag[i];
        num += p * (double) i * fs / (double) n;
        den += p;
    }
    return den > 0.0 ? num / den : 0.0;
}

/** Frequency of the strongest bin in [loHz, hiHz], refined by parabolic interpolation. */
double peakHz (const std::vector<double>& mag, double fs, size_t n, double loHz, double hiHz)
{
    const size_t lo = (size_t) (loHz * (double) n / fs), hi = std::min (mag.size() - 2, (size_t) (hiHz * (double) n / fs));
    size_t best = lo;
    for (size_t i = lo; i <= hi; ++i) if (mag[i] > mag[best]) best = i;
    const double a = std::log (mag[best - 1] + 1e-30), b = std::log (mag[best] + 1e-30), c = std::log (mag[best + 1] + 1e-30);
    const double offset = 0.5 * (a - c) / (a - 2.0 * b + c);
    return ((double) best + offset) * fs / (double) n;
}

double energy (const std::vector<float>& x, size_t from, size_t to)
{
    double e = 0.0;
    for (size_t i = from; i < std::min (to, x.size()); ++i) e += (double) x[i] * x[i];
    return e;
}

EngineParams dryParams()
{
    EngineParams p;
    p.windSpeedMs = 0.0f;
    p.windAir = 0.0f;
    p.rainLevel = 0.0f;
    p.rainRateMMh = 0.0f;
    p.rumbleMix = 0.0f;
    p.crackLevel = 0.0f;
    p.airAbsorption = 0.0f;
    p.unisonCents = 0.0f;
    p.stereoWidth = 0.0f;
    p.masterDb = 0.0f;
    return p;
}

/** Renders the engine, calling events(sampleIndex) at each block boundary. */
void render (PetrichorEngine& e, double seconds, std::vector<float>& left, std::vector<float>& right,
             const std::function<void (size_t)>& events = {})
{
    const size_t total = (size_t) (seconds * e.getSampleRate());
    left.assign (total, 0.0f);
    right.assign (total, 0.0f);
    constexpr size_t block = 128;
    for (size_t pos = 0; pos < total; pos += block)
    {
        if (events) events (pos);
        const int n = (int) std::min (block, total - pos);
        e.process (left.data() + pos, right.data() + pos, n);
    }
}

std::vector<float> mono (const std::vector<float>& l, const std::vector<float>& r)
{
    std::vector<float> m (l.size());
    for (size_t i = 0; i < l.size(); ++i) m[i] = 0.5f * (l[i] + r[i]);
    return m;
}

//==============================================================================
void testMarshallPalmer()
{
    std::printf ("Marshall-Palmer drop size distribution\n");
    CHECK (std::abs (atmos::marshallPalmerLambda (1.0f) - 4.1f) < 1e-4f, "Lambda(1) = %f", atmos::marshallPalmerLambda (1.0f));
    CHECK (std::abs (atmos::marshallPalmerLambda (10.0f) - 2.5280f) < 1e-3f, "Lambda(10) = %f", atmos::marshallPalmerLambda (10.0f));
    CHECK (atmos::dropNumberFlux (50.0f) > atmos::dropNumberFlux (5.0f), "flux must grow with R");

    // Sampled diameters follow the flux-weighted spectrum N(D) v_T(D).
    const float R = 20.0f, lambda = atmos::marshallPalmerLambda (R);
    double num = 0.0, den = 0.0;
    for (float d = atmos::kMinDropMM; d <= atmos::kMaxDropMM; d += 0.001f)
    {
        const double w = std::exp (-lambda * d) * atmos::terminalVelocity (d);
        num += w * d;
        den += w;
    }
    const double expected = num / den;

    Rng rng (42);
    double mean = 0.0;
    const int n = 200000;
    for (int i = 0; i < n; ++i) mean += atmos::sampleImpactDiameter (lambda, rng);
    mean /= n;
    CHECK (std::abs (mean - expected) < 0.01 * expected, "mean D %.4f vs %.4f", mean, expected);
    std::printf ("  R=20 mm/h: Lambda=%.3f /mm, flux-weighted mean D=%.3f mm (sampled %.3f)\n", lambda, expected, mean);
}

void testTerminalVelocity()
{
    std::printf ("Terminal velocity and advection\n");
    const float v2 = atmos::terminalVelocity (2.0f);
    CHECK (std::abs (v2 - 6.535f) < 0.01f, "v_T(2mm) = %f", v2);
    CHECK (atmos::terminalVelocity (1.0f) < v2 && v2 < atmos::terminalVelocity (4.0f), "monotonic");
    CHECK (atmos::terminalVelocity (8.0f) <= atmos::kMaxTerminalVelocity, "capped");
    CHECK (std::abs (atmos::impactSpeed (3.0f, 4.0f) - 5.0f) < 1e-5f, "vector sum");
}

void testStrouhal()
{
    std::printf ("Strouhal / obstacle mapping\n");
    CHECK (std::abs (atmos::strouhalFrequency (10.0f, 0.005f) - 400.0f) < 1e-3f, "f = St U / L");
    const float lowL = atmos::noteObstacleLength (27.5f), highL = atmos::noteObstacleLength (4186.0f);
    CHECK (lowL > highL, "low keys are larger obstacles");
    const float fLow = atmos::strouhalFrequency (10.0f, lowL), fHigh = atmos::strouhalFrequency (10.0f, highL);
    std::printf ("  U=10 m/s: A0 LFO corner %.3f Hz, C8 LFO corner %.2f Hz\n", fLow, fHigh);
    CHECK (fLow < 0.3f && fHigh > 10.0f, "brooding bass, volatile treble");
}

void testAbsorption()
{
    std::printf ("Atmospheric absorption\n");
    const float x = 800.0f;
    const float l1 = -std::log (atmos::absorptionGain (1000.0f, x)), l2 = -std::log (atmos::absorptionGain (2000.0f, x));
    CHECK (std::abs (l2 / l1 - 4.0f) < 1e-3f, "alpha ~ f^2 (ratio %f)", l2 / l1);
    CHECK (atmos::absorptionCascadeCutoff (100.0f, 4) > atmos::absorptionCascadeCutoff (1000.0f, 4), "cutoff falls with distance");
    CHECK (atmos::velocityToDistance (127.0f, 1000.0f) == 0.0f, "V=127 is overhead");
    CHECK (std::abs (atmos::velocityToDistance (1.0f, 1000.0f) - 1000.0f) < 1e-3f, "V=1 is farthest");

    // The 4-pole cascade tracks the Gaussian exp(-alpha f^2 x) at low frequencies.
    const float fc = atmos::absorptionCascadeCutoff (x, 4);
    for (float f : { 200.0f, 500.0f, 1000.0f })
    {
        const float cascade = std::pow (1.0f + (f / fc) * (f / fc), -2.0f);
        const float exact = atmos::absorptionGain (f, x);
        CHECK (std::abs (20.0f * std::log10 (cascade / exact)) < 1.0f, "cascade %.3f vs exact %.3f at %.0f Hz", cascade, exact, f);
    }
}

void testKolmogorovSpectrum()
{
    std::printf ("Kolmogorov turbulence (-5/3)\n");
    KolmogorovNoise k;
    k.reset (1234);
    const double tickRate = 1000.0, fc = 5.0;
    const size_t n = 1 << 20;
    std::vector<float> x (n);
    double var = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        x[i] = k.advance ((float) fc, (float) (1.0 / tickRate));
        var += (double) x[i] * x[i];
    }
    var /= (double) n;
    CHECK (std::abs (var - 1.0) < 0.15, "variance %f", var);

    // Welch PSD
    const size_t seg = 1 << 14;
    std::vector<double> psd (seg / 2, 0.0);
    int segments = 0;
    for (size_t s = 0; s + seg <= n; s += seg / 2, ++segments)
    {
        const auto m = magnitudeSpectrum (x, s, seg);
        for (size_t i = 0; i < m.size(); ++i) psd[i] += m[i] * m[i];
    }

    // Least-squares slope of log P vs log f over the inertial range [1.5 fc, 8 fc].
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int count = 0;
    for (size_t i = 1; i < psd.size(); ++i)
    {
        const double f = (double) i * tickRate / (double) seg;
        if (f < 1.5 * fc || f > 8.0 * fc) continue;
        const double lx = std::log10 (f), ly = std::log10 (psd[i] / segments);
        sx += lx; sy += ly; sxx += lx * lx; sxy += lx * ly; ++count;
    }
    const double slope = (count * sxy - sx * sy) / (count * sxx - sx * sx);
    std::printf ("  variance %.3f, inertial-range slope %.3f (target -1.667)\n", var, slope);
    CHECK (std::abs (slope - (-5.0 / 3.0)) < 0.25, "slope %f", slope);

    // Below the corner the spectrum is flat (von Karman energy-containing range).
    auto band = [&] (double lo, double hi) {
        double sum = 0; int c = 0;
        for (size_t i = 1; i < psd.size(); ++i) { const double f = (double) i * tickRate / (double) seg; if (f >= lo && f <= hi) { sum += psd[i]; ++c; } }
        return sum / std::max (c, 1);
    };
    const double flatRatio = band (0.05 * fc, 0.15 * fc) / band (0.2 * fc, 0.4 * fc);
    CHECK (flatRatio > 0.6 && flatRatio < 1.7, "flat below corner, ratio %f", flatRatio);
}

void testPitchAndInharmonicity()
{
    std::printf ("Piano pitch and inharmonicity\n");
    PianoVoice v;
    v.prepare (kFs, 7);
    StrikeSettings s;
    s.unisonCents = 0.0f;
    s.crackLevel = 0.0f;
    s.absorption = 0.0f;

    auto renderKey = [&] (int key, float vel, double seconds) {
        v.prepare (kFs, 7);
        v.strike (key, vel, s);
        std::vector<float> out ((size_t) (seconds * kFs));
        WindState calm;
        VoiceWindSettings ws;
        for (size_t pos = 0; pos < out.size(); pos += 32)
        {
            v.controlTick (calm, ws, 0.0f, 32.0f / (float) kFs);
            v.render (out.data() + pos, (int) std::min<size_t> (32, out.size() - pos));
        }
        return out;
    };

    const auto a4 = renderKey (69, 0.6f, 1.5);
    const size_t n = 1 << 16;
    auto mag = magnitudeSpectrum (a4, 2000, n);
    const double f1 = peakHz (mag, kFs, n, 400.0, 480.0);
    std::printf ("  A4 fundamental %.2f Hz\n", f1);
    CHECK (std::abs (f1 - 440.0) < 0.5, "A4 = %f", f1);

    // C2: partial 12 sits above 12 f1 by sqrt(1 + B 144) / sqrt(1 + B).
    const int key = 36;
    const auto c2 = renderKey (key, 0.8f, 2.0);
    mag = magnitudeSpectrum (c2, 1000, n);
    const double f1c = peakHz (mag, kFs, n, 60.0, 70.0);
    const float B = PianoVoice::inharmonicity (key);
    const double predicted = 12.0 * f1c * std::sqrt (1.0 + B * 144.0) / std::sqrt (1.0 + B);
    const double measured = peakHz (mag, kFs, n, predicted * 0.99, predicted * 1.01);
    std::printf ("  C2 B=%.2e: partial 12 at %.2f Hz (harmonic %.2f, stiff-string prediction %.2f)\n", B, measured, 12.0 * f1c, predicted);
    CHECK (std::abs (measured - predicted) < 0.6, "partial 12 %f vs %f", measured, predicted);
    CHECK (measured - 12.0 * f1c > 1.0, "partials are stretched");
}

void testVelocityIsDistance()
{
    std::printf ("Thunder: velocity -> distance -> absorbed transient\n");
    auto attackCentroid = [] (int velocity) {
        PetrichorEngine e;
        e.prepare (kFs, 512);
        EngineParams p = dryParams();
        p.crackLevel = 1.0f;
        p.airAbsorption = 0.4f;
        p.stormDistanceM = 1500.0f;
        e.setParams (p);
        std::vector<float> l, r;
        render (e, 0.2, l, r, [&] (size_t pos) { if (pos == 0) e.noteOn (60, velocity); });
        const auto m = mono (l, r);
        const size_t n = 2048; // ~43 ms of attack
        return centroidHz (magnitudeSpectrum (m, 0, n), kFs, n);
    };

    const double close = attackCentroid (127), mid = attackCentroid (80), far = attackCentroid (30);
    std::printf ("  attack centroid V=127 %.0f Hz, V=80 %.0f Hz, V=30 %.0f Hz\n", close, mid, far);
    CHECK (close > mid && mid > far, "brightness must fall with distance");
    CHECK (close > 2.0 * far, "close crack vs distant thud: %f / %f", close, far);
}

void testRumbleScalesInverselyWithVelocity()
{
    std::printf ("Thunder: rumble wet mix and decay grow as velocity falls\n");
    auto tailRatio = [] (int velocity) {
        PetrichorEngine e;
        e.prepare (kFs, 512);
        EngineParams p = dryParams();
        p.rumbleMix = 0.6f;
        p.rumbleDecayS = 4.0f;
        e.setParams (p);
        std::vector<float> l, r;
        const size_t off = (size_t) (0.25 * kFs);
        render (e, 3.0, l, r, [&] (size_t pos) {
            if (pos == 0) e.noteOn (48, velocity);
            if (pos >= off && pos < off + 128) e.noteOff (48);
        });
        const auto m = mono (l, r);
        return energy (m, (size_t) (1.0 * kFs), (size_t) (3.0 * kFs)) / energy (m, 0, off);
    };

    const double hard = tailRatio (120), soft = tailRatio (30);
    std::printf ("  tail/direct energy: V=120 %.4f, V=30 %.4f\n", hard, soft);
    CHECK (soft > 3.0 * hard, "soft strikes roll on longer: %f vs %f", soft, hard);
}

void testStrouhalLfoRate()
{
    std::printf ("Wind: note-linked Strouhal LFO rate\n");
    auto crossings = [] (int key) {
        PianoVoice v;
        v.prepare (kFs, 99);
        StrikeSettings s;
        s.decayScale = 3.0f;
        v.strike (key, 0.7f, s);
        WindState w;
        w.meanSpeed = w.speed = 10.0f;
        w.intensity = 0.3f;
        VoiceWindSettings ws;
        int count = 0;
        float prev = 0.0f;
        const float dt = 32.0f / (float) kFs;
        std::vector<float> scratch (32);
        for (int t = 0; t < (int) (8.0f / dt); ++t)
        {
            v.controlTick (w, ws, 0.0f, dt);
            v.render (scratch.data(), 32);
            const float x = v.getWindLfo();
            if ((x > 0.0f) != (prev > 0.0f)) ++count;
            prev = x;
        }
        return count;
    };

    const int low = crossings (33), high = crossings (96);
    std::printf ("  zero crossings in 8 s: A1 %d, C7 %d\n", low, high);
    CHECK (high > 5 * std::max (low, 1), "treble turbulence must be faster");
}

void testRainCoupling()
{
    std::printf ("Rain: wind-coupled Marshall-Palmer granular texture\n");
    auto run = [] (float gustFactor, float windSpeed, double& grainRate, double& centroid) {
        RainTexture rain;
        rain.prepare (kFs, 5);
        RainSettings s;
        s.baseRateMMh = 10.0f;
        s.coupling = 1.0f;
        s.level = 1.0f;
        s.surface = 0.0f;
        s.overlay = 1.0f;
        s.fuse = 0.0f;
        s.follow = 0.0f;
        WindState w;
        w.meanSpeed = 10.0f;
        w.speed = windSpeed;
        w.gustFactor = gustFactor;
        const size_t n = (size_t) (3.0 * kFs);
        std::vector<float> l (n, 0.0f), r (n, 0.0f), pl (32), pr (32);
        for (size_t pos = 0; pos < n; pos += 32)
        {
            rain.controlTick (w, s, PianoFollow{}, 32.0f / (float) kFs);
            rain.beginBlock (32);
            rain.render (l.data() + pos, r.data() + pos, pl.data(), pr.data(), 32);
        }
        grainRate = rain.getGrainRate();
        const auto m = mono (l, r);
        double num = 0, den = 0;
        const size_t seg = 8192;
        for (size_t s0 = (size_t) kFs; s0 + seg <= n; s0 += seg)
        {
            const auto mag = magnitudeSpectrum (m, s0, seg);
            for (size_t i = 1; i < mag.size(); ++i) { num += mag[i] * mag[i] * (double) i * kFs / seg; den += mag[i] * mag[i]; }
        }
        centroid = num / den;
    };

    double lullRate, lullCentroid, gustRate, gustCentroid;
    run (0.6f, 6.0f, lullRate, lullCentroid);
    run (1.5f, 15.0f, gustRate, gustCentroid);
    std::printf ("  lull: %.0f grains/s, centroid %.0f Hz | gust: %.0f grains/s, centroid %.0f Hz\n",
                 lullRate, lullCentroid, gustRate, gustCentroid);
    CHECK (gustRate > 2.0 * lullRate, "gusts bring denser rain");
    CHECK (gustCentroid > 1.3 * lullCentroid, "gusts brighten the texture");
}


void testThunderIsTheHammer()
{
    std::printf ("Thunder: the strike lives in the hammer-string contact\n");
    auto contact = [] (int velocity, float multipath, int& peaks, float& span) {
        PianoVoice v;
        v.prepare (kFs, 11);
        StrikeSettings s;
        s.crackLevel = 0.0f;
        s.multipath = multipath;
        v.strike (48, (float) velocity / 127.0f, s);
        const float* f = v.getForce();
        const int n = v.getForceLength();
        float peak = 0.0f;
        for (int i = 0; i < n; ++i) peak = std::max (peak, f[i]);
        // Count separate contacts: rising crossings of 30 % of the main peak.
        peaks = 0;
        int first = -1, last = 0;
        for (int i = 1; i < n; ++i)
        {
            if (f[i] >= 0.3f * peak && f[i - 1] < 0.3f * peak) ++peaks;
            if (f[i] > 0.01f * peak) { if (first < 0) first = i; last = i; }
        }
        span = (float) (last - std::max (first, 0)) / (float) kFs * 1000.0f;
    };

    int hardPeaks, softPeaks, dryPeaks;
    float hardSpan, softSpan, drySpan;
    contact (127, 1.0f, hardPeaks, hardSpan);
    contact (25, 1.0f, softPeaks, softSpan);
    contact (25, 0.0f, dryPeaks, drySpan);
    std::printf ("  contacts: V=127 %d (%.1f ms) | V=25 %d (%.1f ms) | V=25 without multipath %d (%.1f ms)\n",
                 hardPeaks, hardSpan, softPeaks, softSpan, dryPeaks, drySpan);
    CHECK (hardPeaks == 1, "a close strike is one localised contact (%d)", hardPeaks);
    CHECK (softPeaks >= 3 && softSpan > 2.0f * hardSpan, "a distant strike rolls through several contacts");
    CHECK (dryPeaks == 1, "multipath off: one contact");

    // No separate thunder sound: with the strings removed (crack only, absorbing distance huge),
    // what reaches the output is the strings' response, so a crack-only strike stays pitched.
    PetrichorEngine e;
    e.prepare (kFs, 512);
    EngineParams p = dryParams();
    p.crackLevel = 1.0f;
    e.setParams (p);
    std::vector<float> l, r;
    render (e, 0.5, l, r, [&] (size_t pos) { if (pos == 0) e.noteOn (69, 127); });
    const auto m = mono (l, r);
    const size_t n = 8192;
    const auto mag = magnitudeSpectrum (m, 0, n);
    double onPartials = 0.0, total = 0.0;
    for (size_t i = 1; i < mag.size(); ++i)
    {
        const double f = (double) i * kFs / (double) n;
        const double harmonic = f / 440.0;
        const double p2 = mag[i] * mag[i];
        total += p2;
        if (f > 300.0 && std::abs (harmonic - std::round (harmonic)) < 0.04 * std::max (1.0, std::round (harmonic) * 0.5))
            onPartials += p2;
    }
    std::printf ("  crack energy on string partials: %.1f %%\n", 100.0 * onPartials / total);
    CHECK (onPartials / total > 0.8, "the crack is heard through the string, not beside it");
}

void testWindFuse()
{
    std::printf ("Wind: overlay <-> fuse\n");
    auto pitchSpread = [] (float fuse) {
        PianoVoice v;
        v.prepare (kFs, 21);
        StrikeSettings s;
        s.decayScale = 3.0f;
        v.strike (60, 0.6f, s);
        WindState w;
        w.meanSpeed = 12.0f;
        w.intensity = 0.4f;
        VoiceWindSettings ws;
        ws.fuse = fuse;
        ws.pitch = 1.0f;
        KolmogorovNoise gust;
        gust.reset (5);
        const float dt = 32.0f / (float) kFs;
        std::vector<float> scratch (32);
        double sum = 0.0, sum2 = 0.0;
        int count = 0;
        for (int t = 0; t < (int) (6.0f / dt); ++t)
        {
            w.gust = gust.advance (0.4f, dt);
            w.gustFactor = std::max (0.0f, 1.0f + w.intensity * w.gust);
            w.speed = w.meanSpeed * w.gustFactor;
            v.controlTick (w, ws, 0.0f, dt);
            v.render (scratch.data(), 32);
            if (t * dt > 1.0f)
            {
                const double cents = 1200.0 * std::log2 ((double) v.getPitchRatio());
                sum += cents; sum2 += cents * cents; ++count;
            }
        }
        const double mean = sum / count;
        return std::sqrt (std::max (0.0, sum2 / count - mean * mean));
    };

    const double overlay = pitchSpread (0.0f), fused = pitchSpread (1.0f);
    std::printf ("  pitch deviation (rms cents): overlay %.2f, fused %.2f\n", overlay, fused);
    CHECK (overlay < 0.01, "overlay leaves the note's pitch alone");
    CHECK (fused > 3.0, "fused wind bends the note like an Aeolian tone");
}

void testRainOverlayAndFuse()
{
    std::printf ("Rain: overlay follows the piano, fuse lives inside it\n");

    // Overlay with full follow is silent when the piano is, and present when it plays.
    auto overlayEnergy = [] (float pianoEnvelope) {
        RainTexture rain;
        rain.prepare (kFs, 9);
        RainSettings s;
        s.baseRateMMh = 20.0f; s.level = 1.0f; s.overlay = 1.0f; s.fuse = 0.0f; s.follow = 1.0f;
        WindState w;
        PianoFollow piano { pianoEnvelope, 800.0f };
        const size_t n = (size_t) kFs;
        std::vector<float> l (n, 0.0f), r (n, 0.0f), pl (32), pr (32);
        for (size_t pos = 0; pos < n; pos += 32)
        {
            rain.controlTick (w, s, piano, 32.0f / (float) kFs);
            rain.beginBlock (32);
            rain.render (l.data() + pos, r.data() + pos, pl.data(), pr.data(), 32);
        }
        return energy (l, n / 2, n) + energy (r, n / 2, n);
    };
    const double quiet = overlayEnergy (0.0f), loud = overlayEnergy (1.0f);
    std::printf ("  overlay energy, follow=1: piano silent %.2e, piano playing %.2e\n", quiet, loud);
    CHECK (quiet < 1e-9 && loud > 1e-4, "overlay rain follows the piano");

    // Fused rain makes no sound of its own...
    auto engineRender = [] (float blend, float level, bool play) {
        PetrichorEngine e;
        e.prepare (kFs, 512);
        EngineParams p = dryParams();
        p.rainRateMMh = 25.0f;
        p.rainLevel = level;
        p.rainBlend = blend;
        p.rainFollow = 0.0f;
        p.sustain = 2.0f;
        e.setParams (p);
        std::vector<float> l, r;
        render (e, 3.0, l, r, [&] (size_t pos) { if (play && pos == 0) { e.noteOn (57, 90); e.noteOn (64, 80); } });
        return mono (l, r);
    };
    const auto silentFuse = engineRender (1.0f, 0.8f, false);
    const auto silentOverlay = engineRender (0.0f, 0.8f, false);
    std::printf ("  no notes: fused rain energy %.2e, overlay rain energy %.2e\n",
                 energy (silentFuse, 0, silentFuse.size()), energy (silentOverlay, 0, silentOverlay.size()));
    CHECK (energy (silentFuse, 0, silentFuse.size()) == 0.0, "fused rain only exists inside the piano");
    CHECK (energy (silentOverlay, 0, silentOverlay.size()) > 1e-4, "overlay rain plays on its own");

    // ...but texturises the notes: more upper-band energy and a rougher envelope than the dry notes.
    const auto dry = engineRender (1.0f, 0.0f, true);
    const auto fused = engineRender (1.0f, 0.8f, true);
    auto band = [] (const std::vector<float>& x, double lo, double hi) {
        const size_t seg = 8192;
        double e = 0.0;
        for (size_t s0 = (size_t) (1.5 * kFs); s0 + seg <= x.size(); s0 += seg)
        {
            const auto mag = magnitudeSpectrum (x, s0, seg);
            for (size_t i = 1; i < mag.size(); ++i)
            {
                const double f = (double) i * kFs / seg;
                if (f >= lo && f < hi) e += mag[i] * mag[i];
            }
        }
        return e;
    };
    const double dryHigh = band (dry, 3000.0, 12000.0), fusedHigh = band (fused, 3000.0, 12000.0);
    std::printf ("  sustained notes, 3-12 kHz energy: dry %.3e, rain fused %.3e (%.1f dB)\n",
                 dryHigh, fusedHigh, 10.0 * std::log10 (fusedHigh / dryHigh));
    CHECK (fusedHigh > 2.0 * dryHigh, "fused rain adds texture inside the notes");
}

void testPianoCharacter()
{
    std::printf ("Piano I <-> II: softened grand to Rhodes-style tine\n");
    auto renderVoice = [] (int key, int vel, float character) {
        PianoVoice v;
        v.prepare (kFs, 3);
        StrikeSettings s;
        s.character = character;
        s.crackLevel = 0.5f;
        s.multipath = 0.0f;
        s.absorption = 0.0f;
        v.strike (key, (float) vel / 127.0f, s);
        std::vector<float> out ((size_t) (1.5 * kFs));
        WindState calm;
        VoiceWindSettings ws;
        for (size_t pos = 0; pos < out.size(); pos += 32)
        {
            v.controlTick (calm, ws, 0.0f, 32.0f / (float) kFs);
            v.render (out.data() + pos, (int) std::min<size_t> (32, out.size() - pos));
        }
        return out;
    };

    const size_t n = 1 << 15;
    const auto grand = renderVoice (60, 90, 0.0f), tine = renderVoice (60, 90, 1.0f);
    const auto gm = magnitudeSpectrum (grand, 1000, n), tm = magnitudeSpectrum (tine, 1000, n);
    const double gc = centroidHz (gm, kFs, n), tc = centroidHz (tm, kFs, n);
    std::printf ("  C4 V=90 spectral centroid: I %.0f Hz, II %.0f Hz\n", gc, tc);
    CHECK (tc < 0.7 * gc, "II is rounder than I");

    // II's partials are harmonic (a tine through a pickup); I's are stretched (stiff string).
    const double f1 = peakHz (tm, kFs, n, 250.0, 275.0);
    const double h3 = peakHz (tm, kFs, n, 3.0 * f1 * 0.99, 3.0 * f1 * 1.01);
    std::printf ("  II: f1 %.2f Hz, partial 3 at %.2f Hz (3 f1 = %.2f)\n", f1, h3, 3.0 * f1);
    CHECK (std::abs (h3 - 3.0 * f1) < 0.4, "II partials are harmonic");

    // II barks harder when played harder: more 2nd harmonic relative to the fundamental.
    auto secondHarmonic = [&] (int vel) {
        const auto x = renderVoice (60, vel, 1.0f);
        const auto m = magnitudeSpectrum (x, 500, n);
        const double a1 = m[(size_t) std::lround (f1 * n / kFs)], a2 = m[(size_t) std::lround (2.0 * f1 * n / kFs)];
        return 20.0 * std::log10 (a2 / a1);
    };
    const double soft = secondHarmonic (30), hard = secondHarmonic (127);
    std::printf ("  II 2nd harmonic: V=30 %.1f dB, V=127 %.1f dB\n", soft, hard);
    CHECK (hard > soft + 6.0, "the tine barks when struck hard");

    // Sympathetic strings (I): with the pedal down the piano keeps ringing after a staccato note.
    auto tail = [] (float resonance) {
        PetrichorEngine e;
        e.prepare (kFs, 512);
        EngineParams p = dryParams();
        p.resonance = resonance;
        e.setParams (p);
        std::vector<float> l, r;
        render (e, 2.0, l, r, [&] (size_t pos) {
            if (pos == 0) { e.setSustainPedal (true); e.noteOn (72, 100); }
        });
        const auto m = mono (l, r);
        return energy (m, (size_t) (0.5 * kFs), (size_t) (2.0 * kFs));
    };
    const double dry = tail (0.0f), ringing = tail (1.0f);
    std::printf ("  pedal-down tail energy: resonance off %.3e, on %.3e\n", dry, ringing);
    CHECK (ringing > 1.05 * dry, "free strings ring along");
}

void testStabilityAndSilence()
{
    std::printf ("Engine stability\n");
    {
        PetrichorEngine e;
        e.prepare (kFs, 512);
        EngineParams p;
        p.rainLevel = 0.0f;
        p.windAir = 0.0f;
        e.setParams (p);
        std::vector<float> l, r;
        render (e, 1.0, l, r);
        CHECK (energy (l, 0, l.size()) == 0.0 && energy (r, 0, r.size()) == 0.0, "silent when idle");
    }
    {
        PetrichorEngine e;
        e.prepare (96000.0, 512);
        EngineParams p;
        p.windSpeedMs = 30.0f; p.turbulence = 1.0f; p.windPitch = 1.0f; p.windTimbre = 1.0f; p.windAir = 1.0f; p.windBlend = 0.5f;
        p.rainBlend = 0.5f; p.rainFollow = 1.0f; p.character = 0.5f; p.resonance = 1.0f;
        p.rainRateMMh = 150.0f; p.rainCoupling = 1.0f; p.rainLevel = 1.0f; p.rainSurface = 1.0f;
        p.rumbleMix = 1.0f; p.rumbleDecayS = 12.0f; p.crackLevel = 1.0f; p.sustain = 3.0f; p.hammerHardness = 1.0f;
        e.setParams (p);
        Rng rng (3);
        std::vector<float> l, r;
        render (e, 12.0, l, r, [&] (size_t pos) {
            if (pos % 2048 == 0) e.noteOn (21 + (int) (rng.uniform() * 88), 1 + (int) (rng.uniform() * 126));
            if (pos % 3072 == 0) e.noteOff (21 + (int) (rng.uniform() * 88));
            if (pos % 96000 == 0) e.setSustainPedal ((pos / 96000) % 2 == 0);
        });
        bool finite = true;
        float peak = 0.0f;
        for (size_t i = 0; i < l.size(); ++i)
        {
            finite &= std::isfinite (l[i]) && std::isfinite (r[i]);
            peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
        }
        CHECK (finite, "NaN/inf in output");
        CHECK (peak <= 1.0f, "peak %f", peak);
        std::printf ("  extreme settings, 96 kHz, 12 s: finite=%d peak=%.3f voices=%d\n", (int) finite, peak, e.getTelemetry().activeVoices);
    }
}

void testPerformance()
{
    std::printf ("Performance\n");
    PetrichorEngine e;
    e.prepare (kFs, 512);
    EngineParams p;
    p.sustain = 3.0f;
    e.setParams (p);
    e.setSustainPedal (true);
    std::vector<float> l, r;
    int key = 21;
    const auto t0 = std::chrono::steady_clock::now();
    render (e, 10.0, l, r, [&] (size_t pos) {
        if (pos % 1536 == 0) { e.noteOn (key, 100); key = key >= 108 ? 21 : key + 1; }
    });
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("  10 s with 64 sustained voices, wind, rain, rumble: %.2f s CPU (%.1fx realtime)\n", secs, 10.0 / secs);
    CHECK (secs < 10.0, "must run faster than real time");
}

} // namespace

int main()
{
    testMarshallPalmer();
    testTerminalVelocity();
    testStrouhal();
    testAbsorption();
    testKolmogorovSpectrum();
    testPitchAndInharmonicity();
    testVelocityIsDistance();
    testRumbleScalesInverselyWithVelocity();
    testStrouhalLfoRate();
    testRainCoupling();
    testThunderIsTheHammer();
    testWindFuse();
    testRainOverlayAndFuse();
    testPianoCharacter();
    testStabilityAndSilence();
    testPerformance();

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
