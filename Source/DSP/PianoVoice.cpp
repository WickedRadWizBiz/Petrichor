#include "PianoVoice.h"

namespace petrichor
{

namespace
{
    constexpr int kLowestKey  = 21;  // A0
    constexpr int kHighestKey = 108; // C8
    constexpr int kFirstUndampedKey = 90; // F#6 and up have no dampers

    float keyPosition (int key) noexcept
    {
        return (float) (std::clamp (key, kLowestKey, kHighestKey) - kLowestKey) / (float) (kHighestKey - kLowestKey);
    }

    /** Loudness calibration across the keyboard (measured with `PetrichorRender --calibrate`):
        lifts the shorter-lived treble toward the bass and middle. */
    float keyLoudnessTrim (float kNorm) noexcept
    {
        return dbToGain (9.0f * std::max (0.0f, kNorm - 0.35f));
    }
}

//==============================================================================
float PianoVoice::inharmonicity (int midiKey) noexcept
{
    // log10(B) piecewise-linear over the keyboard, after measured grand-piano data:
    // ~3e-4 in the low bass, a minimum near the wound/plain string break, rising to ~8e-3 at C8.
    const int k = std::clamp (midiKey, kLowestKey, kHighestKey);
    const float logLow = std::log10 (3.0e-4f), logMin = -4.0f, logHigh = std::log10 (8.0e-3f);

    const float logB = (k <= 45) ? lerpf (logLow, logMin, (float) (k - kLowestKey) / 24.0f)
                                 : lerpf (logMin, logHigh, (float) (k - 45) / 63.0f);
    return std::pow (10.0f, logB);
}

float PianoVoice::stretchCents (int midiKey) noexcept
{
    // Railsback curve: octaves stretched by the strings' own inharmonicity.
    const float d = (float) (std::clamp (midiKey, kLowestKey, kHighestKey) - 69);
    return (d < 0.0f ? 2.6e-4f : 4.6e-4f) * d * d * d;
}

float PianoVoice::promptT60 (int midiKey) noexcept
{
    // ~14 s in the low bass down to ~0.6 s at C8 for the fundamental's prompt decay.
    return 14.0f * std::pow (0.045f, keyPosition (midiKey));
}

//==============================================================================
void PianoVoice::prepare (double sampleRate, uint32_t seed)
{
    fs = (float) sampleRate;
    rng.setSeed (seed);
    lfo.reset (seed * 2654435761u + 17u);

    std::fill (std::begin (re), std::end (re), 0.0f);
    std::fill (std::begin (im), std::end (im), 0.0f);
    numModes = 0;
    active = damping = stealing = false;
    transient = Transient{};
    currentRatio = 1.0f;
}

void PianoVoice::strike (int midiKey, float velocity01, const StrikeSettings& s)
{
    const bool continuing = active && ! stealing;

    if (continuing)
    {
        foldEnvelopeIntoState();
    }
    else
    {
        std::fill (std::begin (re), std::end (re), 0.0f);
        std::fill (std::begin (im), std::end (im), 0.0f);
        transient = Transient{};
        currentRatio = 1.0f;
        swellGain = swellTarget = 1.0f;
        swellStep = 0.0f;
        level = 0.0f;
    }

    key = midiKey;
    velocity = clampf (velocity01, 0.0f, 1.0f);

    const int k = std::clamp (midiKey, kLowestKey, kHighestKey);
    const float kNorm = keyPosition (k);
    const float v = std::max (velocity, 1.0f / 127.0f);

    //==========================================================================
    // Thunder: velocity is distance.
    const float midiVelocity = v * 127.0f;
    strikeDistance   = atmos::velocityToDistance (midiVelocity, s.maxDistanceM);
    distanceFraction = clampf ((127.0f - midiVelocity) / 126.0f, 0.0f, 1.0f);
    const float toneDistance = strikeDistance * clampf (s.toneAbsorption, 0.0f, 1.0f);

    //==========================================================================
    // String: stiff-string partials, unison mistuning and frequency-dependent loss.
    fundamentalHz = midiToHz ((float) midiKey, s.a4Hz) * std::exp2 (stretchCents (k) / 1200.0f);
    const float B = inharmonicity (k);
    const float f0 = fundamentalHz / std::sqrt (1.0f + B);
    const float strikePoint = 0.122f - 0.05f * kNorm * kNorm; // ~1/8 of the speaking length
    const float fMax = std::min (0.45f * fs, 14000.0f);

    //==========================================================================
    // Hammer: a gamma-shaped force pulse F(t) ~ t^(k-1) e^(-t/theta). Its spectrum
    // (1 + (f/f_h)^2)^(-k/2) is smooth (no spectral nulls) and causal. Faster, harder strikes
    // compress the felt for less time: f_h rises with hammer speed and the slope k flattens.
    const float hammerSpeed = 0.35f * std::pow (17.0f, v); // ~0.35 .. 6 m/s
    constexpr float referenceSpeed = 2.08f;                // mezzo-forte, V ~ 80
    const float hardness = clampf (s.hammerHardness, 0.0f, 1.0f) - (s.softPedal ? 0.25f : 0.0f);
    const float cornerRef = 650.0f * std::exp2 (0.6f * (float) (k - 60) / 12.0f) * std::exp2 ((hardness - 0.5f) * 1.6f);
    const float corner = cornerRef * std::pow (hammerSpeed / referenceSpeed, 0.6f);
    const float order = 2.6f - 1.0f * v;
    constexpr float orderRef = 2.6f - 0.63f;
    const float impulse = v * (s.softPedal ? 0.75f : 1.0f);

    const float decayScale = std::max (s.decayScale, 0.05f);
    const float sigma1 = 6.91f / (promptT60 (k) * decayScale);
    const float b3 = 2.6e-7f / decayScale;
    const float afterDecay = 0.22f;
    const float afterLevel = s.softPedal ? 0.5f : 0.32f;
    const float unison = std::max (s.unisonCents, 0.0f) * (k < 32 ? 0.5f : 1.0f) * (0.6f + 0.8f * hash01 ((uint32_t) k, 7));

    float normAcc = 0.0f;
    int m = 0;

    for (int n = 1; n <= kMaxPartials; ++n)
    {
        const float fn = (float) n * f0 * std::sqrt (1.0f + B * (float) (n * n));
        if (fn > fMax)
            break;

        const float comb = 0.03f + 0.97f * std::abs (std::sin ((float) n * kPi * strikePoint));
        const float r2 = (fn / 90.0f) * (fn / 90.0f);
        const float radiation = (r2 / (1.0f + r2)) / std::sqrt (1.0f + (fn / 7000.0f) * (fn / 7000.0f));
        const float soundboard = 0.8f + 0.4f * hash01 ((uint32_t) k, 100u + (uint32_t) n);
        const float shape = comb * radiation * soundboard;

        // Loudness is normalised against the mezzo-forte hammer spectrum of this key.
        const float reference = shape * std::pow (1.0f + (fn / cornerRef) * (fn / cornerRef), -0.5f * orderRef);
        normAcc += reference * reference;

        const float g = shape * atmos::absorptionGain (fn, toneDistance);
        const float cents = unison * (0.75f + 0.5f * hash01 ((uint32_t) k, 300u + (uint32_t) n));
        const float sigma = sigma1 + b3 * fn * fn;

        const float modeFreq[2]  = { fn * std::exp2 (cents / 2400.0f), fn * std::exp2 (-cents / 2400.0f) };
        const float modeSigma[2] = { sigma, sigma * afterDecay };
        const float modeDrive[2] = { g, g * afterLevel };

        for (int j = 0; j < kModesPerPartial; ++j, ++m)
        {
            const float w = kTwoPi * modeFreq[j] / fs;
            const float rho = std::exp (-modeSigma[j] / fs);
            omega[m]  = w;
            zr0[m]    = rho * std::cos (w);
            zi0[m]    = rho * std::sin (w);
            drive[m]  = modeDrive[j];
            modeHz[m] = modeFreq[j];
            weight[m] = 1.0f;
        }
    }

    numModes = ((m + kLanes - 1) / kLanes) * kLanes;

    for (int i = m; i < kMaxModes; ++i)
        omega[i] = zr0[i] = zi0[i] = drive[i] = modeHz[i] = weight[i] = re[i] = im[i] = 0.0f;

    const float norm = 1.0f / std::sqrt (std::max (normAcc, 1.0e-9f));
    for (int i = 0; i < m; ++i)
        drive[i] *= norm;

    std::copy (std::begin (zr0), std::end (zr0), std::begin (zr));
    std::copy (std::begin (zi0), std::end (zi0), std::begin (zi));
    if (std::abs (currentRatio - 1.0f) > 1.0e-7f)
    {
        const float ratio = currentRatio;
        currentRatio = 1.0f;
        applyPitchRatio (ratio);
    }
    lastCentre = lastEmphasis = -1.0f;

    //==========================================================================
    // Render the hammer's force pulse. Hammers in a chord never land on the same sample: up to
    // 1 ms of action jitter keeps simultaneous MIDI notes from summing into one coherent spike.
    const int jitter = (int) (rng.uniform() * 0.001f * fs);
    const float theta = 1.0f / (kTwoPi * corner);
    pulseLength = std::clamp (jitter + (int) std::lround ((order + 5.0f) * theta * fs), jitter + 2, kMaxPulse);
    pulsePos = 0;
    float pulseSum = 0.0f;
    for (int i = 0; i < pulseLength; ++i)
    {
        const float t = ((float) (i - jitter) + 0.5f) / (fs * theta);
        pulse[i] = i < jitter ? 0.0f : std::pow (t, order - 1.0f) * std::exp (-t);
        pulseSum += pulse[i];
    }
    const float pulseScale = impulse / std::max (pulseSum, 1.0e-9f);
    for (int i = 0; i < pulseLength; ++i)
        pulse[i] *= pulseScale;

    // Tension modulation: loud notes start slightly sharp and settle.
    glideDepth = 0.0011f * v * v;
    timeSinceStrike = 0.0f;

    //==========================================================================
    // Lightning: S(t) = A e^(-t/tau) n(t), plus the soundboard knock, through atmospheric absorption.
    {
        auto& t = transient;
        const float crack = std::max (s.crackLevel, 0.0f) * impulse;
        const float tauNoise = lerpf (0.014f, 0.005f, kNorm);
        const float tauThump = 0.035f * (1.0f - 0.5f * kNorm);
        const float thumpHz = 70.0f + 90.0f * kNorm;

        t.noiseAmp   = crack * 1.1f;
        t.noiseDecay = std::exp (-1.0f / (tauNoise * fs));

        const float rho = std::exp (-1.0f / (tauThump * fs));
        t.thumpZr = rho * std::cos (kTwoPi * thumpHz / fs);
        t.thumpZi = rho * std::sin (kTwoPi * thumpHz / fs);
        t.thumpRe += crack * 0.8f;

        t.samplesLeft = (int) (fs * std::max (14.0f * tauNoise, 7.0f * tauThump));
        t.highPass.setCutoff (40.0f, fs);

        const float cutoff = atmos::absorptionCascadeCutoff (strikeDistance, 4);
        for (auto& lp : t.absorb)
            lp.setCutoff (cutoff, fs);

        t.delay = jitter;
        t.active = crack > 0.0f;
    }

    outputGain = 0.1f * keyLoudnessTrim (kNorm);
    env = 1.0f;
    damping = false;
    stealing = false;
    stealGain = 1.0f;
    dampCoef = 1.0f;
    energy = std::max (energy, 1.0e-6f);
    active = true;
}

void PianoVoice::startDamper() noexcept
{
    if (! active || damping || key >= kFirstUndampedKey)
        return;

    // Felt dampers: quick in the treble, slower on the heavy bass strings.
    const float t60 = 0.6f * std::pow (0.2f, (float) (std::clamp (key, kLowestKey, kFirstUndampedKey) - kLowestKey) / 68.0f);
    dampCoef = std::exp (-6.91f / (t60 * fs));
    damping = true;
}

void PianoVoice::beginSteal() noexcept
{
    if (! active)
        return;
    stealing = true;
    stealStep = 1.0f / std::max (1.0f, 0.004f * fs); // 4 ms
}

void PianoVoice::foldEnvelopeIntoState() noexcept
{
    if (env == 1.0f)
        return;
    for (int i = 0; i < numModes; ++i)
    {
        re[i] *= env;
        im[i] *= env;
    }
    env = 1.0f;
}

//==============================================================================
void PianoVoice::applyPitchRatio (float ratio) noexcept
{
    ratio = clampf (ratio, 0.97f, 1.03f);
    const float d = ratio - 1.0f;

    for (int i = 0; i < numModes; ++i)
    {
        // Rotate the base pole by theta = omega * (ratio - 1); |theta| is small, so a short series is exact enough.
        const float th = omega[i] * d;
        const float t2 = th * th;
        const float c = 1.0f - t2 * (0.5f - t2 * (1.0f / 24.0f));
        const float sn = th * (1.0f - t2 * ((1.0f / 6.0f) - t2 * (1.0f / 120.0f)));
        zr[i] = zr0[i] * c - zi0[i] * sn;
        zi[i] = zr0[i] * sn + zi0[i] * c;
    }
    currentRatio = ratio;
}

void PianoVoice::updateFilterWeights (float centreHz, float emphasis) noexcept
{
    constexpr float q = 1.6f;
    const float inv = 1.0f / std::max (centreHz, 1.0f);
    const float makeup = 1.0f / (1.0f + 0.3f * emphasis);

    for (int i = 0; i < numModes; ++i)
    {
        if (modeHz[i] <= 0.0f)
        {
            weight[i] = 0.0f;
            continue;
        }
        const float r = modeHz[i] * inv;
        const float x = q * (r - 1.0f / r);
        weight[i] = (1.0f + emphasis / std::sqrt (1.0f + x * x)) * makeup;
    }
    lastCentre = centreHz;
    lastEmphasis = emphasis;
}

void PianoVoice::trimSilentModes() noexcept
{
    while (numModes > kLanes)
    {
        float peak = 0.0f;
        for (int i = numModes - kLanes; i < numModes; ++i)
            peak = std::max (peak, re[i] * re[i] + im[i] * im[i]);

        if (peak > 1.0e-12f)
            break;

        for (int i = numModes - kLanes; i < numModes; ++i)
            re[i] = im[i] = 0.0f;
        numModes -= kLanes;
    }
}

void PianoVoice::controlTick (const WindState& wind, const VoiceWindSettings& ws, float dt) noexcept
{
    if (! active)
        return;

    timeSinceStrike += dt;

    //==========================================================================
    // Strouhal-scaled turbulence: f_c = St U / L with L the note's wavelength.
    const float obstacle = atmos::noteObstacleLength (fundamentalHz);
    const float corner = clampf (atmos::strouhalFrequency (wind.speed, obstacle), 0.02f, 16.0f);
    lfoValue = lfo.advance (corner, dt);

    const float sigmaU = wind.intensity * wind.speed;           // rms gust velocity at the key
    const float lineOfSight = clampf (ws.drift, 0.0f, 1.0f) * 0.5f * sigmaU * lfoValue;
    const float glide = 1.0f + glideDepth * std::exp (-timeSinceStrike / 0.07f);
    const float ratio = atmos::dopplerRatio (lineOfSight) * glide;

    if (std::abs (ratio - currentRatio) > 1.0e-7f)
        applyPitchRatio (ratio);

    //==========================================================================
    // Resonant band-pass sweeping across the partials; it swells with the gusts.
    const float strength = clampf (sigmaU / 4.0f, 0.0f, 1.5f);
    const float sweep = 0.55f * lfoValue + 0.45f * wind.gust;
    const float centre = clampf (fundamentalHz * 4.0f * std::exp2 (1.2f * strength * sweep), 60.0f, 0.4f * fs);
    const float emphasis = clampf (ws.filter, 0.0f, 1.0f) * 1.4f * std::min (strength, 1.0f) * (0.6f + 0.4f * wind.gustFactor);

    if (lastCentre < 0.0f || std::abs (centre - lastCentre) > 0.004f * lastCentre || std::abs (emphasis - lastEmphasis) > 0.01f)
        updateFilterWeights (centre, emphasis);

    swellTarget = 1.0f + 0.15f * clampf (ws.filter, 0.0f, 1.0f) * std::min (strength, 1.0f) * clampf (sweep, -1.5f, 1.5f);
    swellStep = (swellTarget - swellGain) / std::max (1.0f, dt * fs);

    //==========================================================================
    // Bookkeeping: energy for stealing, trimming of decayed partials, end of life.
    float e = 0.0f;
    for (int i = 0; i < numModes; ++i)
        e += re[i] * re[i] + im[i] * im[i];
    energy = e * env * env;

    const float a = std::exp (-dt / 0.05f);
    level = a * level + (1.0f - a) * std::sqrt (energy) * outputGain * 4.0f;

    const bool hammerDone = pulsePos >= pulseLength;
    if (hammerDone)
        trimSilentModes();

    if (hammerDone && ! transient.active && energy < 1.0e-10f)
        active = false;
}

//==============================================================================
float PianoVoice::tickModes() noexcept
{
    float acc[kLanes] {};

    for (int base = 0; base < numModes; base += kLanes)
    {
        for (int j = 0; j < kLanes; ++j)
        {
            const int i = base + j;
            const float r = re[i], q = im[i];
            const float nr = r * zr[i] - q * zi[i];
            const float nq = r * zi[i] + q * zr[i];
            re[i] = nr;
            im[i] = nq;
            acc[j] += nq * weight[i];
        }
    }

    return ((acc[0] + acc[1]) + (acc[2] + acc[3])) + ((acc[4] + acc[5]) + (acc[6] + acc[7]));
}

float PianoVoice::tickModesDriven (float force) noexcept
{
    float acc[kLanes] {};

    for (int base = 0; base < numModes; base += kLanes)
    {
        for (int j = 0; j < kLanes; ++j)
        {
            const int i = base + j;
            const float r = re[i], q = im[i];
            const float nr = r * zr[i] - q * zi[i] + force * drive[i];
            const float nq = r * zi[i] + q * zr[i];
            re[i] = nr;
            im[i] = nq;
            acc[j] += nq * weight[i];
        }
    }

    return ((acc[0] + acc[1]) + (acc[2] + acc[3])) + ((acc[4] + acc[5]) + (acc[6] + acc[7]));
}

float PianoVoice::tickTransient() noexcept
{
    auto& t = transient;
    if (t.delay > 0)
    {
        --t.delay;
        return 0.0f;
    }

    float x = t.highPass.process (rng.bipolar() * 1.7320508f) * t.noiseAmp;
    t.noiseAmp *= t.noiseDecay;

    const float nr = t.thumpRe * t.thumpZr - t.thumpIm * t.thumpZi;
    const float ni = t.thumpRe * t.thumpZi + t.thumpIm * t.thumpZr;
    t.thumpRe = nr;
    t.thumpIm = ni;
    x += ni;

    for (auto& lp : t.absorb)
        x = lp.process (x);

    if (--t.samplesLeft <= 0)
    {
        t.active = false;
        t.thumpRe = t.thumpIm = 0.0f;
    }
    return x;
}

void PianoVoice::render (float* out, int numSamples) noexcept
{
    if (! active)
    {
        std::fill (out, out + numSamples, 0.0f);
        return;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        float y = (pulsePos < pulseLength) ? tickModesDriven (pulse[pulsePos++]) : tickModes();

        if ((swellStep > 0.0f && swellGain < swellTarget) || (swellStep < 0.0f && swellGain > swellTarget))
            swellGain += swellStep;

        if (damping)
            env *= dampCoef;

        y *= swellGain * env;

        if (transient.active)
            y += tickTransient();

        y *= outputGain;

        if (stealing)
        {
            stealGain -= stealStep;
            if (stealGain <= 0.0f)
            {
                std::fill (out + i, out + numSamples, 0.0f);
                active = false;
                stealing = false;
                std::fill (std::begin (re), std::end (re), 0.0f);
                std::fill (std::begin (im), std::end (im), 0.0f);
                transient = Transient{};
                return;
            }
            y *= stealGain;
        }

        out[i] = y;
    }
}

} // namespace petrichor
