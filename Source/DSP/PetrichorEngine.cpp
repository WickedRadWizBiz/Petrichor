#include "PetrichorEngine.h"

namespace petrichor
{

namespace
{
    /** Equal-power Overlay <-> Fuse blend. */
    inline float overlayAmount (float blend) noexcept { return std::cos (clampf (blend, 0.0f, 1.0f) * kPi * 0.5f); }
    inline float fuseAmount (float blend) noexcept    { return std::sin (clampf (blend, 0.0f, 1.0f) * kPi * 0.5f); }

    /** Piano bus RMS that counts as "moderately loud" for the rain's follow envelope. */
    constexpr float kFollowReference = 0.04f;
}

void PetrichorEngine::prepare (double newSampleRate, int /*maxBlockSize*/)
{
    sampleRate = newSampleRate;

    uint32_t seed = 0x5EED1234u;
    for (auto& slot : voices)
    {
        slot.voice.prepare (sampleRate, seed += 0x9E3779B9u);
        slot.keyHeld = slot.hasPending = false;
    }

    wind.reset (0xC0FFEEu);
    rain.prepare (sampleRate, 0xDA1Bu);
    air.prepare (sampleRate, 0xA1Bu);
    rumble.prepare (sampleRate, 0x7E11u);

    sympathetic.prepare (sampleRate);
    for (auto* b : { &voiceBuffer, &sideBuffer, &pianoMono, &pianoLeft, &pianoRight, &patterLeft, &patterRight })
        b->assign (kControlBlock, 0.0f);
    for (auto& b : sendBuffers)
        b.assign (kControlBlock, 0.0f);

    pianoFollow = PianoFollow{};
    followEnergy = followSlopeEnergy = 0.0;
    followSamples = 0;
    followPrevious = 0.0f;

    samplesUntilControl = 0;
    voiceGainSmoothed = dbToGain (params.pianoLevelDb);
    masterGainSmoothed = dbToGain (params.masterDb);
    telemetry = EngineTelemetry{};
}

void PetrichorEngine::reset()
{
    allSoundOff();
}

//==============================================================================
StrikeSettings PetrichorEngine::makeStrikeSettings() const noexcept
{
    StrikeSettings s;
    s.a4Hz           = params.tuningA4Hz;
    s.hammerHardness = params.hammerHardness;
    s.decayScale     = params.sustain;
    s.unisonCents    = params.unisonCents;
    s.maxDistanceM   = params.stormDistanceM;
    s.absorption     = params.airAbsorption;
    s.crackLevel     = params.crackLevel;
    s.multipath      = params.rumbleMix;
    s.character      = params.character;
    s.softPedal      = softPedal;
    return s;
}

int PetrichorEngine::findVoiceForKey (int key) const noexcept
{
    for (int i = 0; i < kMaxVoices; ++i)
    {
        const auto& slot = voices[(size_t) i];
        if (slot.hasPending && slot.pendingKey == key)
            return i;
        if (! slot.hasPending && slot.voice.isActive() && ! slot.voice.isStealing() && slot.voice.getKey() == key)
            return i;
    }
    return -1;
}

int PetrichorEngine::findFreeOrStealVoice() noexcept
{
    for (int i = 0; i < kMaxVoices; ++i)
        if (! voices[(size_t) i].voice.isActive() && ! voices[(size_t) i].hasPending)
            return i;

    // Steal by kinetic energy: the quietest released string goes first, then the quietest held one.
    int best = -1;
    float bestEnergy = 0.0f;
    for (int pass = 0; pass < 2 && best < 0; ++pass)
    {
        for (int i = 0; i < kMaxVoices; ++i)
        {
            const auto& slot = voices[(size_t) i];
            if (slot.hasPending || slot.voice.isStealing())
                continue;
            if (pass == 0 && slot.keyHeld)
                continue;
            const float e = slot.voice.getEnergy();
            if (best < 0 || e < bestEnergy)
            {
                best = i;
                bestEnergy = e;
            }
        }
    }

    if (best < 0) // everything is already being stolen: queue behind the first slot
        best = 0;

    voices[(size_t) best].voice.beginSteal();
    return best;
}

void PetrichorEngine::noteOn (int midiKey, int midiVelocity) noexcept
{
    if (midiVelocity <= 0)
    {
        noteOff (midiKey);
        return;
    }

    const float velocity01 = clampf ((float) midiVelocity / 127.0f, 0.0f, 1.0f);
    int index = findVoiceForKey (midiKey);

    if (index >= 0 && ! voices[(size_t) index].hasPending)
    {
        // Re-striking a sounding string adds to its motion (linear superposition).
        voices[(size_t) index].voice.strike (midiKey, velocity01, makeStrikeSettings());
    }
    else
    {
        if (index < 0)
            index = findFreeOrStealVoice();

        auto& slot = voices[(size_t) index];
        if (slot.voice.isActive())
        {
            slot.hasPending = true;
            slot.pendingKey = midiKey;
            slot.pendingVelocity = velocity01;
        }
        else
        {
            slot.hasPending = false;
            slot.voice.strike (midiKey, velocity01, makeStrikeSettings());
        }
    }

    auto& slot = voices[(size_t) index];
    slot.keyHeld = true;
    slot.startOrder = ++noteCounter;

    telemetry.strikeCount++;
    telemetry.lastStrikeKey = (float) midiKey;
    telemetry.lastStrikeVelocity = (float) midiVelocity;
    telemetry.lastStrikeDistance = atmos::velocityToDistance ((float) midiVelocity, params.stormDistanceM);
}

void PetrichorEngine::noteOff (int midiKey) noexcept
{
    for (auto& slot : voices)
    {
        const bool matches = slot.hasPending ? slot.pendingKey == midiKey
                                             : (slot.voice.isActive() && slot.voice.getKey() == midiKey);
        if (! matches || ! slot.keyHeld)
            continue;

        slot.keyHeld = false;
        if (! sustainPedal && ! slot.hasPending)
            slot.voice.startDamper();
    }
}

void PetrichorEngine::setSustainPedal (bool down) noexcept
{
    sustainPedal = down;
    if (down)
        return;

    for (auto& slot : voices)
        if (! slot.keyHeld && ! slot.hasPending && slot.voice.isActive())
            slot.voice.startDamper();
}

void PetrichorEngine::allNotesOff() noexcept
{
    sustainPedal = softPedal = false;
    for (auto& slot : voices)
    {
        slot.keyHeld = false;
        slot.hasPending = false;
        slot.voice.startDamper();
    }
}

void PetrichorEngine::allSoundOff() noexcept
{
    for (auto& slot : voices)
    {
        slot.voice.kill();
        slot.keyHeld = slot.hasPending = false;
    }
    rumble.clear();
    sympathetic.clear();
    sustainPedal = softPedal = false;
}

//==============================================================================
float PetrichorEngine::keyPan (int key) const noexcept
{
    // Player's perspective: bass on the left, treble on the right.
    return 0.85f * clampf (params.stereoWidth, 0.0f, 1.0f) * clampf (((float) key - 64.5f) / 43.5f, -1.0f, 1.0f);
}

void PetrichorEngine::controlTick (float dt) noexcept
{
    wind.setParameters (params.windSpeedMs, params.turbulence, params.gustLengthM);
    const WindState& ws = wind.tick (dt);

    // What the rain hears of the piano: envelope (fast attack, slow release) and spectral centre.
    if (followSamples > 0)
    {
        const float rms = (float) std::sqrt (followEnergy / followSamples);
        const float target = rms / kFollowReference;
        const float tc = target > pianoFollow.envelope ? 0.01f : 0.3f;
        pianoFollow.envelope += (target - pianoFollow.envelope) * (1.0f - std::exp (-dt / tc));

        if (followEnergy > 1.0e-12)
        {
            // RMS frequency: sqrt(E[x'^2] / E[x^2]) * fs / 2 pi.
            const float centroid = (float) std::sqrt (followSlopeEnergy / followEnergy) * (float) sampleRate / kTwoPi;
            pianoFollow.centroidHz += (centroid - pianoFollow.centroidHz) * (1.0f - std::exp (-dt / 0.1f));
        }
        followEnergy = followSlopeEnergy = 0.0;
        followSamples = 0;
    }

    RainSettings rs;
    rs.baseRateMMh = params.rainRateMMh;
    rs.coupling    = params.rainCoupling;
    rs.level       = params.rainLevel;
    rs.surface     = params.rainSurface;
    rs.overlay     = overlayAmount (params.rainBlend);
    rs.fuse        = fuseAmount (params.rainBlend);
    rs.follow      = params.rainFollow;
    rain.controlTick (ws, rs, pianoFollow, dt);

    air.controlTick (ws, params.windAir * overlayAmount (params.windBlend), dt);

    // Free strings ring along with the grand (I); a Rhodes (II) has none.
    sympathetic.setTuning (params.tuningA4Hz);
    sympathetic.setState (params.resonance * (1.0f - clampf (params.character, 0.0f, 1.0f)), sustainPedal, dt);

    rumble.setDecay (params.rumbleDecayS);
    rumble.controlTick (ws, dt);

    VoiceWindSettings vw;
    vw.fuse   = fuseAmount (params.windBlend);
    vw.pitch  = params.windPitch;
    vw.timbre = params.windTimbre;
    const float rainHiss = rain.getFusedHiss();

    const StrikeSettings strikeSettings = makeStrikeSettings();

    for (auto& slot : voices)
    {
        if (slot.hasPending && ! slot.voice.isActive())
        {
            slot.hasPending = false;
            slot.voice.strike (slot.pendingKey, slot.pendingVelocity, strikeSettings);
            if (! slot.keyHeld && ! sustainPedal)
                slot.voice.startDamper();
        }

        if (slot.voice.isActive())
            slot.voice.controlTick (ws, vw, rainHiss, dt);
    }
}

void PetrichorEngine::landDrops() noexcept
{
    // Fused rain: each drop lands on a ringing string, more often on the louder ones.
    const float gain = rain.getFusedDropGain();
    if (gain <= 0.0f)
        return;

    float total = 0.0f;
    for (const auto& slot : voices)
        if (slot.voice.isActive() && ! slot.voice.isStealing())
            total += std::sqrt (slot.voice.getEnergy());
    if (total <= 0.0f)
        return;

    const DropEvent* events = rain.getEvents();
    for (int e = 0; e < numDropEvents; ++e)
    {
        float pick = dropRng.uniform() * total;
        for (auto& slot : voices)
        {
            if (! slot.voice.isActive() || slot.voice.isStealing())
                continue;
            pick -= std::sqrt (slot.voice.getEnergy());
            if (pick <= 0.0f)
            {
                slot.voice.rainDrop (RainTexture::fusedDropStrength (events[e].amount) * gain, events[e].centreHz);
                break;
            }
        }
    }
}

void PetrichorEngine::renderBlock (float* left, float* right, int n) noexcept
{
    std::fill (left, left + n, 0.0f);
    std::fill (right, right + n, 0.0f);
    std::fill (pianoLeft.begin(), pianoLeft.begin() + n, 0.0f);
    std::fill (pianoRight.begin(), pianoRight.begin() + n, 0.0f);
    for (auto& b : sendBuffers)
        std::fill (b.begin(), b.begin() + n, 0.0f);

    numDropEvents = rain.beginBlock (n);
    landDrops();

    const float voiceGainTarget = dbToGain (params.pianoLevelDb);
    const float masterTarget = dbToGain (params.masterDb);
    const float rumbleMix = clampf (params.rumbleMix, 0.0f, 1.0f);

    const float voiceGainStart = voiceGainSmoothed;
    voiceGainSmoothed += (voiceGainTarget - voiceGainSmoothed) * 0.05f;
    const float voiceGainStep = (voiceGainSmoothed - voiceGainStart) / (float) n;

    for (auto& slot : voices)
    {
        auto& v = slot.voice;
        if (! v.isActive())
            continue;

        v.render (voiceBuffer.data(), sideBuffer.data(), n);

        float pl, pr;
        panGains (keyPan (v.getKey()), pl, pr);

        // Wet mix rises as the strike gets farther away.
        const float d = v.getDistanceFraction();
        float zoneW[MultipathRumble::kZones];
        MultipathRumble::zoneWeights (d, zoneW);
        const float send = rumbleMix * (0.06f + 0.44f * d);
        const float s0 = send * zoneW[0], s1 = send * zoneW[1], s2 = send * zoneW[2];

        const float width = clampf (params.stereoWidth, 0.0f, 1.0f);
        float g = voiceGainStart;
        for (int i = 0; i < n; ++i)
        {
            const float y = voiceBuffer[(size_t) i] * g;
            const float sd = sideBuffer[(size_t) i] * g * width;
            g += voiceGainStep;
            pianoLeft[(size_t) i]  += y * pl + sd;
            pianoRight[(size_t) i] += y * pr - sd;
            sendBuffers[0][(size_t) i] += y * s0;
            sendBuffers[1][(size_t) i] += y * s1;
            sendBuffers[2][(size_t) i] += y * s2;
        }
    }

    // Sympathetic strings ring along with the dry piano.
    for (int i = 0; i < n; ++i)
        pianoMono[(size_t) i] = 0.5f * (pianoLeft[(size_t) i] + pianoRight[(size_t) i]);
    sympathetic.process (pianoMono.data(), pianoLeft.data(), pianoRight.data(), n);

    // Rain overlay goes straight to the mix; its patter modulates the piano when fused.
    rain.render (left, right, patterLeft.data(), patterRight.data(), n);
    const float patterDepth = rain.getPatterDepth();

    for (int i = 0; i < n; ++i)
    {
        const float pl = pianoLeft[(size_t) i], pr = pianoRight[(size_t) i];

        // Measure the dry piano for the rain's follow envelope and spectral centre.
        const float mono = 0.5f * (pl + pr);
        const float slope = mono - followPrevious;
        followPrevious = mono;
        followEnergy += (double) mono * mono;
        followSlopeEnergy += (double) slope * slope;

        left[i]  += pl * clampf (1.0f + patterDepth * patterLeft[(size_t) i], 0.3f, 1.7f);
        right[i] += pr * clampf (1.0f + patterDepth * patterRight[(size_t) i], 0.3f, 1.7f);
    }
    followSamples += n;

    air.render (left, right, n);

    const float* sends[MultipathRumble::kZones] = { sendBuffers[0].data(), sendBuffers[1].data(), sendBuffers[2].data() };
    rumble.process (sends, left, right, n);

    // Master gain with a soft knee above -2 dBFS.
    const float masterStart = masterGainSmoothed;
    masterGainSmoothed += (masterTarget - masterGainSmoothed) * 0.05f;
    const float masterStep = (masterGainSmoothed - masterStart) / (float) n;
    float mg = masterStart;

    auto shape = [] (float x) noexcept
    {
        constexpr float knee = 0.8f;
        const float a = std::abs (x);
        if (a <= knee)
            return x;
        const float y = knee + (1.0f - knee) * std::tanh ((a - knee) / (1.0f - knee));
        return x < 0.0f ? -y : y;
    };

    for (int i = 0; i < n; ++i)
    {
        left[i]  = shape (left[i] * mg);
        right[i] = shape (right[i] * mg);
        mg += masterStep;
    }
}

void PetrichorEngine::process (float* left, float* right, int numSamples) noexcept
{
    DenormalGuard denormalGuard;
    const float controlDt = (float) kControlBlock / (float) sampleRate;

    int done = 0;
    while (done < numSamples)
    {
        if (samplesUntilControl <= 0)
        {
            controlTick (controlDt);
            samplesUntilControl = kControlBlock;
        }

        const int chunk = std::min (numSamples - done, samplesUntilControl);
        renderBlock (left + done, right + done, chunk);
        done += chunk;
        samplesUntilControl -= chunk;
    }

    // Telemetry for the visualiser.
    const auto& ws = wind.current();
    telemetry.windSpeed   = ws.speed;
    telemetry.windGust    = ws.gust;
    telemetry.gustFactor  = ws.gustFactor;
    telemetry.rainRate    = rain.getRainRate();
    telemetry.lambda      = rain.getLambda();
    telemetry.grainRate   = rain.getGrainRate();
    telemetry.meanDropMM  = rain.getMeanDiameter();
    telemetry.impactSpeed = rain.getMeanImpactSpeed();

    int active = 0;
    std::array<float, 88> levels {};
    for (const auto& slot : voices)
    {
        if (! slot.voice.isActive())
            continue;
        ++active;
        const int k = slot.voice.getKey() - 21;
        if (k >= 0 && k < 88)
            levels[(size_t) k] = std::max (levels[(size_t) k], slot.voice.getLevel());
    }
    telemetry.activeVoices = active;
    telemetry.keyLevels = levels;
}

} // namespace petrichor
