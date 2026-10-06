// Offline renderer: plays scripted performances through the engine and writes WAV files.
//   PetrichorRender <output-dir>            render the demo pieces
//   PetrichorRender --calibrate [I/II]      print per-key loudness at a fixed velocity (0 = I, 1 = II)

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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

// 2. Wind, Overlay -> Fuse: the same held chords while the blend sweeps from the audible wind
//    layer (0-8 s) into the notes themselves (pitch bending like an Aeolian tone, timbre sweeping).
void windOverlayToFuse (const std::string& dir)
{
    Score s;
    const double bar = 8.0;
    for (int i = 0; i < 4; ++i)
    {
        const double t = 0.4 + bar * i;
        if (i > 0) s.pedalChange (t); else s.pedal (0.0, true);
        s.chord (t, { 38, 45, 53, 57, 62, 69 }, 66, bar, 0.05);
        s.note (t + 4.0, i % 2 ? 81 : 76, 58, 4.0);
    }
    s.pedal (bar * 4 + 0.4, false);

    EngineParams p;
    p.sustain = 2.4f;
    p.windSpeedMs = 14.0f;
    p.turbulence = 0.7f;
    p.gustLengthM = 6.0f;
    p.windPitch = 0.4f;
    p.windTimbre = 0.7f;
    p.windAir = 0.45f;
    p.rainLevel = 0.0f;
    p.rumbleMix = 0.3f;

    renderScore (dir + "/02_wind_overlay_to_fuse.wav", s, bar * 4 + 4.0, p, [bar] (double t, EngineParams& q) {
        q.windBlend = (float) std::clamp ((t - bar) / (bar * 2.0), 0.0, 1.0);
    });
}

// 3. Rain, Overlay -> Fuse: a slow phrase while the blend sweeps from a rain layer that follows
//    the piano (0-8 s) to rain that only exists inside the notes (from ~24 s).
void rainOverlayToFuse (const std::string& dir)
{
    Score s;
    const double bar = 4.0;
    const std::initializer_list<int> chords[4] = { { 45, 52, 60, 64 }, { 41, 48, 57, 60 }, { 43, 50, 59, 62 }, { 40, 47, 55, 59 } };
    s.pedal (0.0, true);
    for (int i = 0; i < 8; ++i)
    {
        const double t = 0.4 + bar * i;
        if (i > 0) s.pedalChange (t);
        s.chord (t, chords[i % 4], 72, bar, 0.04);
        s.note (t + 2.0, 72 + (i % 3) * 2, 64, 2.0);
    }
    s.pedal (bar * 8 + 0.4, false);

    EngineParams p;
    p.sustain = 1.8f;
    p.windSpeedMs = 9.0f;
    p.turbulence = 0.6f;
    p.windBlend = 1.0f;
    p.rainRateMMh = 18.0f;
    p.rainCoupling = 0.8f;
    p.rainLevel = 0.55f;
    p.rainSurface = 0.4f;
    p.rainFollow = 0.8f;
    p.rumbleMix = 0.3f;

    renderScore (dir + "/03_rain_overlay_to_fuse.wav", s, bar * 8 + 4.0, p, [bar] (double t, EngineParams& q) {
        q.rainBlend = (float) std::clamp ((t - 2.0 * bar) / (bar * 4.0), 0.0, 1.0);
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
    p.windTimbre = 0.5f;
    p.windAir = 0.25f;
    p.rainCoupling = 0.75f;
    p.rainLevel = 0.4f;
    p.rainSurface = 0.45f;

    const double end = t + 10.0;
    renderScore (dir + "/04_storm_prelude.wav", s, end, p, [end] (double now, EngineParams& q) {
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
    renderScore (dir + "/05_dry_piano.wav", s, t + 2.0, p);
}

// 6. Piano I -> II: the same phrase as the softened grand (I), halfway, and the Rhodes-style tine (II).
void pianoCharacter (const std::string& dir)
{
    Score s;
    const double section = 9.0;
    for (int pass = 0; pass < 3; ++pass)
    {
        const double t0 = 0.4 + section * pass;
        s.pedal (t0 - 0.05, true);
        s.chord (t0, { 50, 57, 60, 64 }, 60, 3.0, 0.03);
        s.note (t0 + 0.5, 76, 70, 0.6);
        s.note (t0 + 1.0, 74, 55, 0.6);
        s.note (t0 + 1.5, 72, 85, 0.9);
        s.pedalChange (t0 + 3.0);
        s.chord (t0 + 3.0, { 45, 52, 59, 64, 67 }, 50, 3.0, 0.04);
        s.note (t0 + 3.6, 79, 110, 1.2); // a hard hit: listen for the bark on II
        s.note (t0 + 5.0, 71, 30, 1.5);  // a soft one: near-sine on II
        s.pedal (t0 + 7.5, false);
    }

    EngineParams p;
    p.windSpeedMs = 3.0f;
    p.windAir = 0.0f;
    p.rainLevel = 0.0f;
    p.rumbleMix = 0.2f;
    p.sustain = 1.2f;
    renderScore (dir + "/06_piano_I_to_II.wav", s, section * 3 + 1.0, p, [section] (double t, EngineParams& q) {
        q.character = (float) std::min (1.0, 0.5 * std::floor (t / section));
    });
}

//==============================================================================
void calibrate (float character)
{
    std::printf ("key  rms_dB(0.5s, >100Hz)\n");
    for (int key = 21; key <= 108; key += 3)
    {
        PetrichorEngine e;
        e.prepare (kSampleRate, 256);
        EngineParams p;
        p.windSpeedMs = 0.0f; p.windAir = 0.0f; p.rainLevel = 0.0f; p.rumbleMix = 0.0f;
        p.character = character; p.resonance = 0.0f;
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
        calibrate (argc > 2 ? (float) std::atof (argv[2]) : 0.0f);
        return 0;
    }

    velocityIsDistance (arg);
    windOverlayToFuse (arg);
    rainOverlayToFuse (arg);
    stormPrelude (arg);
    dryPiano (arg);
    pianoCharacter (arg);
    return 0;
}
