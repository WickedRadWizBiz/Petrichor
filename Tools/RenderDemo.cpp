// Offline renderer: plays scripted performances through the engine and writes WAV files.
//   PetrichorRender <output-dir>            render the demo pieces
//   PetrichorRender --calibrate             print per-key loudness at a fixed velocity

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "../Source/DSP/PetrichorEngine.h"
#include "WavWriter.h"

using namespace petrichor;

namespace
{
constexpr int kSampleRate = 48000;

struct Event
{
    double time;
    enum Type { On, Off, PedalDown, PedalUp } type;
    int key = 0, velocity = 0;
};

struct Score
{
    std::vector<Event> events;

    void note (double t, int key, int velocity, double duration)
    {
        events.push_back ({ t, Event::On, key, velocity });
        events.push_back ({ t + duration, Event::Off, key, 0 });
    }

    /** Rolled chord: notes enter low to high, `roll` seconds apart. */
    void chord (double t, std::initializer_list<int> keys, int velocity, double duration, double roll = 0.02, int velocitySpread = 6)
    {
        int i = 0;
        for (int k : keys)
        {
            const int v = std::clamp (velocity + ((i * 7) % (2 * velocitySpread + 1)) - velocitySpread, 1, 127);
            note (t + roll * i, k, v, duration - roll * i);
            ++i;
        }
    }

    void pedal (double t, bool down) { events.push_back ({ t, down ? Event::PedalDown : Event::PedalUp }); }

    /** Lift and re-catch the pedal, as a pianist does at a harmony change. */
    void pedalChange (double t) { pedal (t - 0.02, false); pedal (t + 0.06, true); }
};

using Automation = std::function<void (double, EngineParams&)>;

void renderScore (const std::string& path, Score score, double seconds, const EngineParams& base, const Automation& automate = {})
{
    std::stable_sort (score.events.begin(), score.events.end(), [] (const Event& a, const Event& b) { return a.time < b.time; });

    PetrichorEngine engine;
    engine.prepare (kSampleRate, 256);
    engine.setParams (base);

    const size_t total = (size_t) (seconds * kSampleRate);
    std::vector<float> left (total), right (total);
    size_t next = 0, pos = 0;
    constexpr size_t block = 64;

    float peak = 0.0f;
    while (pos < total)
    {
        const double now = (double) pos / kSampleRate;
        if (automate)
        {
            EngineParams p = base;
            automate (now, p);
            engine.setParams (p);
        }

        while (next < score.events.size() && score.events[next].time <= now)
        {
            const auto& e = score.events[next++];
            switch (e.type)
            {
                case Event::On:        engine.noteOn (e.key, e.velocity); break;
                case Event::Off:       engine.noteOff (e.key); break;
                case Event::PedalDown: engine.setSustainPedal (true); break;
                case Event::PedalUp:   engine.setSustainPedal (false); break;
            }
        }

        const int n = (int) std::min (block, total - pos);
        engine.process (left.data() + pos, right.data() + pos, n);
        for (int i = 0; i < n; ++i)
            peak = std::max ({ peak, std::abs (left[pos + (size_t) i]), std::abs (right[pos + (size_t) i]) });
        pos += (size_t) n;
    }

    // Short fade at the very end.
    const size_t fade = std::min (total, (size_t) (0.5 * kSampleRate));
    for (size_t i = 0; i < fade; ++i)
    {
        const float g = (float) i / (float) fade;
        left[total - 1 - i] *= g;
        right[total - 1 - i] *= g;
    }

    if (tools::writeWav24 (path, left, right, kSampleRate))
        std::printf ("wrote %s  (%.1f s, peak %.1f dBFS)\n", path.c_str(), seconds, 20.0 * std::log10 (std::max (peak, 1.0e-9f)));
    else
        std::printf ("FAILED to write %s\n", path.c_str());
}

//==============================================================================
// 1. Same chord, five velocities: from an overhead crack to a far-off roll.
void velocityIsDistance (const std::string& dir)
{
    Score s;
    const int velocities[] = { 127, 100, 72, 44, 18 };
    double t = 0.3;
    for (int v : velocities)
    {
        s.chord (t, { 38, 45, 50, 57, 62, 65 }, v, 2.4, 0.0, 0);
        t += 4.2;
    }

    EngineParams p;
    p.windSpeedMs = 4.0f;
    p.rainRateMMh = 2.0f;
    p.rainLevel = 0.2f;
    p.windAir = 0.05f;
    p.rumbleMix = 0.5f;
    p.crackLevel = 0.7f;
    renderScore (dir + "/01_velocity_is_distance.wav", s, t + 3.0, p);
}

// 2. One long chord held through rising, gusty wind; the rain breathes with the gusts.
void windSustain (const std::string& dir)
{
    Score s;
    s.pedal (0.0, true);
    s.chord (0.5, { 31, 38, 46, 53, 57, 62, 69 }, 70, 1.0, 0.04);
    s.chord (10.5, { 74, 77, 81 }, 52, 1.0, 0.08);
    s.chord (19.0, { 72, 76, 79 }, 48, 1.0, 0.08);
    s.pedal (27.0, false);

    EngineParams p;
    p.sustain = 2.4f;
    p.turbulence = 0.7f;
    p.gustLengthM = 5.0f;
    p.windDrift = 0.55f;
    p.windFilter = 0.8f;
    p.windAir = 0.25f;
    p.rainRateMMh = 12.0f;
    p.rainCoupling = 0.9f;
    p.rainLevel = 0.45f;
    p.rumbleMix = 0.3f;

    renderScore (dir + "/02_wind_sustain.wav", s, 30.0, p, [] (double t, EngineParams& q) {
        q.windSpeedMs = (float) (3.0 + 17.0 * std::min (1.0, t / 18.0));
    });
}

// 3. A short storm prelude in D minor.
void stormPrelude (const std::string& dir)
{
    Score s;
    const double bar = 3.6;
    double t = 0.4;

    struct Harmony { std::initializer_list<int> left; std::initializer_list<int> right; };
    const Harmony harmonies[] = {
        { { 38, 45, 53 }, { 57, 62, 64, 69 } },  // Dm(add9)
        { { 34, 41, 50 }, { 57, 60, 65, 69 } },  // Bbmaj7
        { { 31, 38, 46 }, { 53, 57, 62, 65 } },  // Gm9
        { { 33, 40, 49 }, { 55, 61, 64, 67 } },  // A7
    };
    const int melody[2][8] = {
        { 69, 72, 74, 76, 77, 76, 74, 72 },
        { 81, 79, 77, 76, 74, 73, 76, 69 },
    };

    s.pedal (0.0, true);
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int h = 0; h < 4; ++h)
        {
            if (t > 0.5)
                s.pedalChange (t);

            const int baseVel = pass == 0 ? 34 + 6 * h : 58 + 8 * h;
            s.chord (t, harmonies[h].left, baseVel, bar, 0.06);
            s.chord (t + bar * 0.5, harmonies[h].right, baseVel - 8, bar * 0.5, 0.05);

            // Melody: two notes per bar.
            const int mv = pass == 0 ? 50 : 82;
            s.note (t + 0.02, melody[pass][h * 2], mv, bar * 0.5);
            s.note (t + bar * 0.5 + 0.02, melody[pass][h * 2 + 1], mv - 6, bar * 0.5);
            t += bar;
        }

        if (pass == 0)
        {
            // The strike: a fortissimo low octave right overhead.
            s.pedalChange (t);
            s.chord (t, { 26, 38 }, 127, 2.2, 0.0, 0);
            s.note (t + 1.3, 86, 26, 1.0);
            s.note (t + 1.9, 81, 20, 1.0);
            t += 3.6;
        }
    }

    // Coda: distant rumbles under a high, soft Dm.
    s.pedalChange (t);
    s.chord (t, { 26, 38, 45 }, 22, 6.0, 0.15);
    s.chord (t + 0.8, { 74, 77, 81, 86 }, 30, 6.0, 0.22);
    s.pedal (t + 9.0, false);

    EngineParams p;
    p.sustain = 1.4f;
    p.rumbleMix = 0.42f;
    p.rumbleDecayS = 6.0f;
    p.crackLevel = 0.6f;
    p.turbulence = 0.45f;
    p.windFilter = 0.5f;
    p.windAir = 0.12f;
    p.rainCoupling = 0.75f;
    p.rainLevel = 0.4f;
    p.rainSurface = 0.45f;

    const double end = t + 10.0;
    renderScore (dir + "/03_storm_prelude.wav", s, end, p, [end] (double now, EngineParams& q) {
        const double x = std::min (1.0, now / (end * 0.6));
        q.windSpeedMs = (float) (4.0 + 12.0 * x);
        q.rainRateMMh = (float) (3.0 + 25.0 * x);
    });
}

// 4. The bare instrument: no weather, moderate rumble off.
void dryPiano (const std::string& dir)
{
    Score s;
    double t = 0.3;
    for (int k : { 21, 33, 45, 57, 69, 81, 93, 105 })
    {
        s.note (t, k, 90, 1.6);
        t += 1.8;
    }
    for (int v : { 20, 45, 70, 95, 120 })
    {
        s.chord (t, { 48, 55, 64, 72 }, v, 1.8, 0.0, 0);
        t += 2.2;
    }

    EngineParams p;
    p.windSpeedMs = 0.0f;
    p.windAir = 0.0f;
    p.rainLevel = 0.0f;
    p.rumbleMix = 0.15f;
    p.crackLevel = 0.4f;
    renderScore (dir + "/04_dry_piano.wav", s, t + 2.0, p);
}

//==============================================================================
void calibrate()
{
    std::printf ("key  rms_dB(0.5s, >100Hz)\n");
    for (int key = 21; key <= 108; key += 3)
    {
        PetrichorEngine e;
        e.prepare (kSampleRate, 256);
        EngineParams p;
        p.windSpeedMs = 0.0f; p.windAir = 0.0f; p.rainLevel = 0.0f; p.rumbleMix = 0.0f;
        p.crackLevel = 0.0f; p.airAbsorption = 0.0f; p.stereoWidth = 0.0f; p.masterDb = 0.0f;
        e.setParams (p);
        e.noteOn (key, 80);

        const size_t n = kSampleRate / 2;
        std::vector<float> l (n), r (n);
        for (size_t pos = 0; pos < n; pos += 64)
            e.process (l.data() + pos, r.data() + pos, (int) std::min<size_t> (64, n - pos));

        // Crude loudness: 2nd-order high-pass at 100 Hz, then RMS.
        OnePoleHP h1, h2;
        h1.setCutoff (100.0f, kSampleRate);
        h2.setCutoff (100.0f, kSampleRate);
        double sum = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const float y = h2.process (h1.process (0.5f * (l[i] + r[i])));
            sum += (double) y * y;
        }
        std::printf ("%3d  %6.1f\n", key, 10.0 * std::log10 (sum / (double) n + 1e-20));
    }
}

} // namespace

int main (int argc, char** argv)
{
    const std::string arg = argc > 1 ? argv[1] : ".";
    if (arg == "--calibrate")
    {
        calibrate();
        return 0;
    }

    velocityIsDistance (arg);
    windSustain (arg);
    stormPrelude (arg);
    dryPiano (arg);
    return 0;
}
