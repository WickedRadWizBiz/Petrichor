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

    /** Magnitude of a second-order band-pass centred on centreHz. */
    inline float bandPassMagnitude (float hz, float centreHz, float q) noexcept
    {
        const float r = hz / centreHz;
        const float x = q * (r - 1.0f / r);
        return 1.0f / std::sqrt (1.0f + x * x);
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
    // Room for the slowest felt pulse (~60 ms) plus 1 ms jitter, the 40 ms multipath span and a tail.
    maxExcitation = (int) std::ceil (0.06f * fs);
    maxForce = maxExcitation + (int) std::ceil (0.045f * fs) + 2;
    force.assign ((size_t) maxForce, 0.0f);
    scratch.assign ((size_t) maxForce, 0.0f);

    std::fill (std::begin (re), std::end (re), 0.0f);
    std::fill (std::begin (im), std::end (im), 0.0f);
    numModes = 0;
    forceLength = forcePos = 0;
    active = damping = stealing = false;
    thump = Thump{};
    currentRatio = 1.0f;
    hissAmp = 0.0f;
    energy = level = 0.0f;
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
        thump = Thump{};
        currentRatio = 1.0f;
        referenceEnergy = 0.0f;
        swellGain = swellTarget = 1.0f;
        swellStep = 0.0f;
        level = 0.0f;
        hissAmp = 0.0f;
    }

    key = midiKey;
    velocity = clampf (velocity01, 0.0f, 1.0f);

    const int k = std::clamp (midiKey, kLowestKey, kHighestKey);
    const float kNorm = keyPosition (k);
    const float v = std::max (velocity, 1.0f / 127.0f);

    //==========================================================================
    // Thunder: velocity is distance, and distance is absorption: P(f, x) = P0(f) exp(-alpha(f) x).
    const float midiVelocity = v * 127.0f;
    strikeDistance   = atmos::velocityToDistance (midiVelocity, s.maxDistanceM);
    distanceFraction = clampf ((127.0f - midiVelocity) / 126.0f, 0.0f, 1.0f);
    const float absorbingDistance = strikeDistance * clampf (s.absorption, 0.0f, 1.0f);
    const float d = distanceFraction;

    //==========================================================================
    // String: stiff-string partials, unison mistuning and frequency-dependent loss.
    fundamentalHz = midiToHz ((float) midiKey, s.a4Hz) * std::exp2 (stretchCents (k) / 1200.0f);
    const float B = inharmonicity (k);
    const float f0 = fundamentalHz / std::sqrt (1.0f + B);
    const float strikePoint = 0.122f - 0.05f * kNorm * kNorm; // ~1/8 of the speaking length
    const float fMax = std::min (0.45f * fs, 14000.0f);

    //==========================================================================
    // Hammer: a gamma-shaped felt pulse F(t) ~ t^(k-1) e^(-t/theta). Its spectrum
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

    // Distant strikes leave a louder, longer aftersound: decay and "wet" share rise as V falls.
    const float decayScale = std::max (s.decayScale, 0.05f);
    const float sigma1 = 6.91f / (promptT60 (k) * decayScale);
    const float b3 = 2.6e-7f / decayScale;
    const float afterDecay = 0.22f * (1.0f - 0.3f * d);
    const float afterLevel = (s.softPedal ? 0.5f : 0.32f) * (1.0f + 0.5f * d);
    const float previousCentre = lastCentre, previousEmphasis = lastEmphasis, previousTilt = lastTilt;
    const float unison = std::max (s.unisonCents, 0.0f) * (k < 32 ? 0.5f : 1.0f) * (0.6f + 0.8f * hash01 ((uint32_t) k, 7));

    float normAcc = 0.0f, hissAcc = 0.0f, hammerEnergy = 0.0f;
    int m = 0;
    referenceDecay = 2.0f * sigma1 * afterDecay; // energy decay rate of the slowest mode

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

        const float g = shape * atmos::absorptionGain (fn, absorbingDistance);
        const float cents = unison * (0.75f + 0.5f * hash01 ((uint32_t) k, 300u + (uint32_t) n));
        const float sigma = sigma1 + b3 * fn * fn;

        // Rain hiss excites the upper partials most: rain is not a bass sound.
        const float rainShape = (fn / 400.0f) / std::sqrt (1.0f + (fn / 400.0f) * (fn / 400.0f))
                              / std::sqrt (1.0f + (fn / 6000.0f) * (fn / 6000.0f));

        const float modeFreq[2]  = { fn * std::exp2 (cents / 2400.0f), fn * std::exp2 (-cents / 2400.0f) };
        const float modeSigma[2] = { sigma, sigma * afterDecay };
        const float modeDrive[2] = { g, g * afterLevel };

        // Energy this strike will put in the partial: |drive x felt-pulse spectrum|^2 (analytic, open loop).
        const float pulseMagnitude = impulse * std::pow (1.0f + (fn / corner) * (fn / corner), -0.5f * order);
        hammerEnergy += (g * pulseMagnitude) * (g * pulseMagnitude) * (1.0f + afterLevel * afterLevel);

        for (int j = 0; j < kModesPerPartial; ++j, ++m)
        {
            const float w = kTwoPi * modeFreq[j] / fs;
            const float rho = std::exp (-modeSigma[j] / fs);
            omega[m]   = w;
            zr0[m]     = rho * std::cos (w);
            zi0[m]     = rho * std::sin (w);
            drive[m]   = modeDrive[j];
            modeHz[m]  = modeFreq[j];
            octaves[m] = std::log2 (modeFreq[j] / fundamentalHz);
            weight[m]  = 1.0f;

            // Unit-variance noise then settles each mode at |drive x rainShape|, whatever its Q.
            hissDrive[m] = shape * rainShape * (j == 0 ? 1.0f : afterLevel) * std::sqrt (std::max (1.0f - rho * rho, 0.0f));
            const float steady = shape * rainShape * (j == 0 ? 1.0f : afterLevel);
            hissAcc += steady * steady;
        }
    }

    numModes = ((m + kLanes - 1) / kLanes) * kLanes;

    for (int i = m; i < kMaxModes; ++i)
        omega[i] = zr0[i] = zi0[i] = drive[i] = hissDrive[i] = modeHz[i] = octaves[i] = weight[i] = re[i] = im[i] = 0.0f;

    const float norm = 1.0f / std::sqrt (std::max (normAcc, 1.0e-9f));

    // Reference envelope for fused rain: strikes add energy; it never reads the live string.
    strikeEnergy = hammerEnergy * norm * norm;
    referenceEnergy = (continuing ? referenceEnergy : 0.0f) + strikeEnergy;
    const float hissNorm = 1.0f / std::sqrt (std::max (hissAcc, 1.0e-9f)); // total steady hiss energy = hissAmp^2
    for (int i = 0; i < m; ++i)
    {
        drive[i] *= norm;
        hissDrive[i] *= hissNorm;
    }

    std::copy (std::begin (zr0), std::end (zr0), std::begin (zr));
    std::copy (std::begin (zi0), std::end (zi0), std::begin (zi));
    if (std::abs (currentRatio - 1.0f) > 1.0e-7f)
    {
        const float ratio = currentRatio;
        currentRatio = 1.0f;
        applyPitchRatio (ratio);
    }
    // A re-struck string keeps its wind weighting (same key, same partials): no click.
    if (continuing && previousCentre >= 0.0f)
    {
        updateWeights (previousCentre, previousEmphasis, previousTilt);
    }
    else
    {
        lastCentre = lastEmphasis = -1.0f;
        lastTilt = 0.0f;
    }

    //==========================================================================
    // The hammer-string contact, lightning crack and multipath included.
    buildForce (s, kNorm, corner, order, impulse, strikePoint);

    // Tension modulation: loud notes start slightly sharp and settle.
    glideDepth = 0.0011f * v * v;
    timeSinceStrike = 0.0f;

    // The soundboard knock is the only part of the strike heard directly.
    {
        const float knock = std::max (s.crackLevel, 0.0f) * impulse * 0.5f;
        const float tauThump = 0.035f * (1.0f - 0.5f * kNorm);
        const float thumpHz = 70.0f + 90.0f * kNorm;
        const float rho = std::exp (-1.0f / (tauThump * fs));
        thump.zr = rho * std::cos (kTwoPi * thumpHz / fs);
        thump.zi = rho * std::sin (kTwoPi * thumpHz / fs);
        thump.re += knock;
        thump.samplesLeft = (int) (fs * 7.0f * tauThump);
        thump.active = knock > 0.0f;
    }

    outputGain = 0.1f * keyLoudnessTrim (kNorm);
    env = 1.0f;
    damping = false;
    stealing = false;
    stealGain = 1.0f;
    dampCoef = 1.0f;
    energy = std::max ((continuing ? energy : 0.0f) + strikeEnergy, 1.0e-6f);
    active = true;
}

void PianoVoice::buildForce (const StrikeSettings& s, float kNorm, float corner, float order, float impulse, float strikePoint)
{
    const float d = distanceFraction;
    const int jitter = (int) (rng.uniform() * 0.001f * fs); // hammers in a chord never land on one sample

    //--------------------------------------------------------------------------
    // e(t): the felt's gamma pulse (the lightning impulse) plus the crack S(t) = A e^(-t/tau) n(t).
    const float theta = 1.0f / (kTwoPi * corner);
    const int pulseLength = std::clamp ((int) std::lround ((order + 5.0f) * theta * fs), 2, maxExcitation);
    const float tauCrack = lerpf (0.010f, 0.004f, kNorm);
    const int crackLength = std::clamp ((int) (4.0f * tauCrack * fs), 1, maxExcitation);
    const int excitationLength = std::max (pulseLength, crackLength);

    float pulseSum = 0.0f;
    for (int i = 0; i < pulseLength; ++i)
    {
        const float t = ((float) i + 0.5f) / (fs * theta);
        scratch[i] = std::pow (t, order - 1.0f) * std::exp (-t);
        pulseSum += scratch[i];
    }
    const float pulseScale = impulse / std::max (pulseSum, 1.0e-9f);
    for (int i = 0; i < pulseLength; ++i)
        scratch[i] *= pulseScale;
    std::fill (scratch.begin() + pulseLength, scratch.begin() + excitationLength, 0.0f);

    // Crack: broadband, ~10 dB under the pulse's low-frequency content, so it rules the upper partials.
    const float crack = std::max (s.crackLevel, 0.0f);
    if (crack > 0.0f)
    {
        // Harder strikes crack more: the felt stiffens and the contact turns noisy.
        const float amp = crack * 0.35f * impulse * std::pow (impulse, 1.5f) / std::sqrt (0.5f * tauCrack * fs);
        const float decay = std::exp (-1.0f / (tauCrack * fs));
        float e = amp;
        for (int i = 0; i < crackLength; ++i)
        {
            scratch[i] += e * rng.bipolar() * 1.7320508f;
            e *= decay;
        }
    }

    //--------------------------------------------------------------------------
    // h(t): multipath. The pulse returns from the near termination after one round trip
    // (strike point -> agraffe -> strike point = strikePoint / f1) and re-contacts the felt.
    // Close strikes stay a single localised contact; distant ones roll through several delayed,
    // progressively darker contacts with irregular spacing - the time-varying delay distribution.
    const float depth = clampf (s.multipath, 0.0f, 1.0f) * d;
    const int taps = 1 + (int) std::lround (7.0f * depth);
    const float roundTrip = strikePoint / std::max (fundamentalHz, 1.0f);
    const float spacing = roundTrip + depth * 0.004f;
    const float maxSpan = 0.04f * fs;

    float gains[8] {}, delays[8] {}, cutoffs[8] {};
    float gainSum = 0.0f, g = 1.0f, t = 0.0f;
    const float ratio = 0.55f + 0.3f * depth;
    for (int tap = 0; tap < taps; ++tap)
    {
        if (tap > 0)
            t += spacing * fs * (0.6f + 0.8f * rng.uniform());
        if (t > maxSpan)
            break;
        delays[tap] = t;
        gains[tap] = g;
        cutoffs[tap] = std::min (0.45f * fs, corner * 6.0f / (1.0f + 0.9f * (float) tap));
        gainSum += g;
        g *= ratio;
    }

    const int span = (int) t + 1;
    forceLength = std::min (maxForce, jitter + span + excitationLength + (int) (0.002f * fs));
    std::fill (force.begin(), force.begin() + forceLength, 0.0f);

    for (int tap = 0; tap < taps && gains[tap] > 0.0f; ++tap)
    {
        const int offset = jitter + (int) delays[tap];
        const float gain = gains[tap] / gainSum;
        OnePoleLP darken;
        darken.setCutoff (tap == 0 ? 1.0e9f : cutoffs[tap], fs);
        for (int i = 0; offset + i < forceLength; ++i)
        {
            const float x = i < excitationLength ? scratch[i] : 0.0f;
            force[offset + i] += gain * darken.process (x);
        }
    }
    forcePos = 0;
    thump.delay = jitter;
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

void PianoVoice::kill() noexcept
{
    std::fill (std::begin (re), std::end (re), 0.0f);
    std::fill (std::begin (im), std::end (im), 0.0f);
    numModes = 0;
    forceLength = forcePos = 0;
    active = damping = stealing = false;
    thump = Thump{};
    currentRatio = 1.0f;
    hissAmp = referenceEnergy = 0.0f;
    energy = level = 0.0f;
    env = 1.0f;
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
    const float dr = ratio - 1.0f;

    for (int i = 0; i < numModes; ++i)
    {
        // Rotate the base pole by theta = omega * (ratio - 1); |theta| is small, so a short series is exact enough.
        const float th = omega[i] * dr;
        const float t2 = th * th;
        const float c = 1.0f - t2 * (0.5f - t2 * (1.0f / 24.0f));
        const float sn = th * (1.0f - t2 * ((1.0f / 6.0f) - t2 * (1.0f / 120.0f)));
        zr[i] = zr0[i] * c - zi0[i] * sn;
        zi[i] = zr0[i] * sn + zi0[i] * c;
    }
    currentRatio = ratio;
}

void PianoVoice::updateWeights (float centreHz, float emphasis, float tilt) noexcept
{
    constexpr float q = 1.6f;
    const float makeup = 1.0f / (1.0f + 0.3f * emphasis);
    const float centre = std::max (centreHz, 1.0f);

    for (int i = 0; i < numModes; ++i)
    {
        if (modeHz[i] <= 0.0f)
        {
            weight[i] = 0.0f;
            continue;
        }
        const float brightness = tilt == 0.0f ? 1.0f : std::exp2 (tilt * octaves[i]);
        weight[i] = (1.0f + emphasis * bandPassMagnitude (modeHz[i], centre, q)) * makeup * brightness;
    }
    lastCentre = centreHz;
    lastEmphasis = emphasis;
    lastTilt = tilt;
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

void PianoVoice::rainDrop (float amount, float impactCentreHz) noexcept
{
    if (! active || stealing || amount <= 0.0f)
        return;

    // The drop's impact spectrum, imprinted on the string. Each drop adds amount^2 of the strike's
    // reference energy, so the texture follows the note without feeding on itself.
    const float centre = std::max (impactCentreHz, 100.0f);
    float shapeEnergy = 0.0f;
    for (int i = 0; i < numModes; ++i)
    {
        const float g = drive[i] * bandPassMagnitude (std::max (modeHz[i], 1.0f), centre, 0.8f);
        hitShape[i] = g;
        shapeEnergy += g * g;
    }
    if (shapeEnergy <= 1.0e-20f)
        return;

    const float a = amount * std::sqrt (referenceEnergy / shapeEnergy);
    for (int i = 0; i < numModes; ++i)
        re[i] += a * hitShape[i];
}

void PianoVoice::controlTick (const WindState& wind, const VoiceWindSettings& ws, float rainHiss, float dt) noexcept
{
    if (! active)
        return;

    timeSinceStrike += dt;
    const float fuse = clampf (ws.fuse, 0.0f, 1.0f);

    //==========================================================================
    // Local wind at this key: the storm's gust times a per-key Kolmogorov LFO whose corner follows
    // the Strouhal law f_c = St U / L, with the note's wavelength as the obstacle length L.
    const float obstacle = atmos::noteObstacleLength (fundamentalHz);
    const float corner = clampf (atmos::strouhalFrequency (wind.speed, obstacle), 0.02f, 16.0f);
    lfoValue = lfo.advance (corner, dt);

    const float local = wind.meanSpeed > 0.05f ? std::max (0.05f, wind.gustFactor * (1.0f + wind.intensity * lfoValue)) : 1.0f;
    const float lnLocal = std::log (local);

    // Wind pitch: an Aeolian tone tracks f ~ U, so the note follows (U_key / U_mean)^depth.
    // Stronger wind bends further: the depth grows with the mean wind (full at 8 m/s, x2 at 16).
    const float pitchDepth = fuse * clampf (ws.pitch, 0.0f, 1.0f) * 0.06f * clampf (wind.meanSpeed / 8.0f, 0.0f, 2.0f);
    const float glide = 1.0f + glideDepth * std::exp (-timeSinceStrike / 0.07f);
    const float ratio = std::exp (pitchDepth * lnLocal) * glide;

    if (std::abs (ratio - currentRatio) > 1.0e-7f)
        applyPitchRatio (ratio);

    //==========================================================================
    // Wind timbre: gusts brighten, lulls darken, and a gentle resonant band-pass sweeps the partials.
    const float timbre = clampf (ws.timbre, 0.0f, 1.0f);
    const float strength = fuse * clampf (wind.intensity * wind.speed / 4.0f, 0.0f, 1.5f);
    const float sweep = 0.55f * lfoValue + 0.45f * wind.gust;
    const float centre = clampf (fundamentalHz * 4.0f * std::exp2 (1.2f * strength * sweep), 60.0f, 0.4f * fs);
    const float emphasis = timbre * 1.4f * std::min (strength, 1.0f) * (0.6f + 0.4f * wind.gustFactor);
    const float tilt = clampf (fuse * timbre * 1.5f * lnLocal, -0.6f, 0.6f);

    if (lastCentre < 0.0f || std::abs (centre - lastCentre) > 0.004f * lastCentre
        || std::abs (emphasis - lastEmphasis) > 0.01f || std::abs (tilt - lastTilt) > 0.005f)
        updateWeights (centre, emphasis, tilt);

    swellTarget = 1.0f + 0.15f * timbre * std::min (strength, 1.0f) * clampf (sweep, -1.5f, 1.5f);
    swellStep = (swellTarget - swellGain) / std::max (1.0f, dt * fs);

    //==========================================================================
    // Bookkeeping: energy for stealing and rain, trimming of decayed partials, end of life.
    float e = 0.0f;
    for (int i = 0; i < numModes; ++i)
        e += re[i] * re[i] + im[i] * im[i];
    energy = e * env * env;
    if (forcePos < forceLength)
        energy = std::max (energy, strikeEnergy); // not yet delivered: don't look stealable

    // Reference envelope for the fused rain: decays at the string's slowest rate and with the damper.
    const bool contactOver = forcePos >= forceLength;
    referenceEnergy *= std::exp (-referenceDecay * dt);
    if (damping)
        referenceEnergy *= std::pow (dampCoef, 2.0f * dt * fs);

    // Fused rain hiss: random-amplitude impulses once per control period are white noise to the
    // modes (scaled so the per-sample variance matches), settling each mode at hissAmp x hissDrive.
    hissAmp = clampf (rainHiss, 0.0f, 0.6f) * std::sqrt (referenceEnergy);
    if (hissAmp > 1.0e-7f)
    {
        const float scale = hissAmp * std::sqrt (std::max (1.0f, dt * fs));
        float lanes[kLanes];
        for (auto& l : lanes)
            l = rng.bipolar() * 1.7320508f * scale;
        for (int i = 0; i < numModes; ++i)
            re[i] += lanes[i & (kLanes - 1)] * hissDrive[i];
    }

    const float a = std::exp (-dt / 0.05f);
    level = a * level + (1.0f - a) * std::sqrt (energy) * outputGain * 4.0f;

    if (contactOver)
        trimSilentModes();

    if (contactOver && ! thump.active && energy < 1.0e-10f)
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

float PianoVoice::tickModesDriven (float f) noexcept
{
    float acc[kLanes] {};

    for (int base = 0; base < numModes; base += kLanes)
    {
        for (int j = 0; j < kLanes; ++j)
        {
            const int i = base + j;
            const float r = re[i], q = im[i];
            const float nr = r * zr[i] - q * zi[i] + f * drive[i];
            const float nq = r * zi[i] + q * zr[i];
            re[i] = nr;
            im[i] = nq;
            acc[j] += nq * weight[i];
        }
    }

    return ((acc[0] + acc[1]) + (acc[2] + acc[3])) + ((acc[4] + acc[5]) + (acc[6] + acc[7]));
}

float PianoVoice::tickThump() noexcept
{
    auto& t = thump;
    if (t.delay > 0)
    {
        --t.delay;
        return 0.0f;
    }

    const float nr = t.re * t.zr - t.im * t.zi;
    const float ni = t.re * t.zi + t.im * t.zr;
    t.re = nr;
    t.im = ni;

    if (--t.samplesLeft <= 0)
    {
        t.active = false;
        t.re = t.im = 0.0f;
    }
    return ni;
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
        const float y0 = forcePos < forceLength ? tickModesDriven (force[(size_t) forcePos++]) : tickModes();
        float y = y0;

        if ((swellStep > 0.0f && swellGain < swellTarget) || (swellStep < 0.0f && swellGain > swellTarget))
            swellGain += swellStep;

        if (damping)
            env *= dampCoef;

        y *= swellGain * env;

        if (thump.active)
            y += tickThump();

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
                thump = Thump{};
                forceLength = forcePos = 0;
                hissAmp = referenceEnergy = energy = 0.0f;
                return;
            }
            y *= stealGain;
        }

        out[i] = y;
    }
}

} // namespace petrichor
