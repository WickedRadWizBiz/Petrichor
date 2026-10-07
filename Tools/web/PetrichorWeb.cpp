// Browser build of the engine: a small C interface, compiled to WebAssembly and driven from an
// AudioWorklet by Tools/web/build_web.py. The parameter table is the plug-in's own
// (Source/Plugin/Parameters.h without JUCE), so ids, ranges and defaults cannot drift.

#define PETRICHOR_SPECS_ONLY 1
#include "../../Source/Plugin/Parameters.h"

#include <cstddef>
#include <cstdlib>
#include <new>

using namespace petrichor;

// The wasm32 C++ runtime is built without exceptions, but its default operator new still refers
// to them. The engine allocates only in prepare(); out of memory there simply aborts.
void* operator new (std::size_t size)
{
    if (void* p = std::malloc (size != 0 ? size : 1))
        return p;
    std::abort();
}
void* operator new[] (std::size_t size) { return operator new (size); }
void* operator new (std::size_t size, std::align_val_t align)
{
    const std::size_t a = (std::size_t) align < sizeof (void*) ? sizeof (void*) : (std::size_t) align;
    if (void* p = std::aligned_alloc (a, (size + a - 1) / a * a))
        return p;
    std::abort();
}
void* operator new[] (std::size_t size, std::align_val_t align) { return operator new (size, align); }
void operator delete (void* p) noexcept                                         { std::free (p); }
void operator delete[] (void* p) noexcept                                       { std::free (p); }
void operator delete (void* p, std::size_t) noexcept                            { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept                          { std::free (p); }
void operator delete (void* p, std::align_val_t) noexcept                       { std::free (p); }
void operator delete[] (void* p, std::align_val_t) noexcept                     { std::free (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept          { std::free (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept        { std::free (p); }

#define PW_EXPORT(name) extern "C" __attribute__ ((export_name (#name), used))

namespace
{
    constexpr int kMaxBlock = 128; // an AudioWorklet render quantum
    constexpr int kTelemetryFields = 13 + 88;

    PetrichorEngine* engine = nullptr;
    EngineParams engineParams;
    float left[kMaxBlock], right[kMaxBlock];
    float telemetry[kTelemetryFields];
}

PW_EXPORT (pw_init) void pw_init (float sampleRate)
{
    if (engine == nullptr)
        engine = new PetrichorEngine();
    engineParams = EngineParams{};
    for (const auto& s : params::all())
        engineParams.*(s.field) = s.defaultValue;
    engine->prepare ((double) sampleRate, kMaxBlock);
    engine->setParams (engineParams);
}

PW_EXPORT (pw_param_count) int pw_param_count() { return (int) params::all().size(); }

PW_EXPORT (pw_param_id) const char* pw_param_id (int index)
{
    const auto& all = params::all();
    return index >= 0 && index < (int) all.size() ? all[(size_t) index].id : "";
}

PW_EXPORT (pw_set_param) void pw_set_param (int index, float value)
{
    const auto& all = params::all();
    if (engine == nullptr || index < 0 || index >= (int) all.size())
        return;
    const auto& s = all[(size_t) index];
    engineParams.*(s.field) = clampf (value, s.min, s.max);
    engine->setParams (engineParams);
}

PW_EXPORT (pw_note_on) void pw_note_on (int key, int velocity) { if (engine) engine->noteOn (key, velocity); }
PW_EXPORT (pw_note_off) void pw_note_off (int key)              { if (engine) engine->noteOff (key); }
PW_EXPORT (pw_sustain) void pw_sustain (int down)               { if (engine) engine->setSustainPedal (down != 0); }
PW_EXPORT (pw_soft) void pw_soft (int down)                     { if (engine) engine->setSoftPedal (down != 0); }
PW_EXPORT (pw_all_notes_off) void pw_all_notes_off()            { if (engine) engine->allNotesOff(); }

PW_EXPORT (pw_left) float* pw_left()   { return left; }
PW_EXPORT (pw_right) float* pw_right() { return right; }

PW_EXPORT (pw_process) void pw_process (int numSamples)
{
    if (engine == nullptr || numSamples <= 0)
        return;
    engine->process (left, right, numSamples < kMaxBlock ? numSamples : kMaxBlock);
}

/** windSpeed, windGust, gustFactor, rainRate, lambda, grainRate, meanDropMM, impactSpeed,
    activeVoices, strikeCount, lastStrikeDistance, lastStrikeKey, lastStrikeVelocity, keyLevels[88]. */
PW_EXPORT (pw_telemetry) float* pw_telemetry()
{
    if (engine == nullptr)
        return telemetry;
    const auto& t = engine->getTelemetry();
    const float head[13] = { t.windSpeed, t.windGust, t.gustFactor, t.rainRate, t.lambda, t.grainRate, t.meanDropMM,
                             t.impactSpeed, (float) t.activeVoices, (float) t.strikeCount, t.lastStrikeDistance,
                             t.lastStrikeKey, t.lastStrikeVelocity };
    for (int i = 0; i < 13; ++i)
        telemetry[i] = head[i];
    for (int i = 0; i < 88; ++i)
        telemetry[13 + i] = t.keyLevels[(size_t) i];
    return telemetry;
}
