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

    /** Loudness calibration of the measured grand (Piano I), dB at each sampled key (A0, C1, D#1 .. C8,
        every third key), from `PetrichorRender --calibrate 0`. The data is normalised to unit energy
        at V = 80 per key; this evens out how loud and how long each recorded key rings. */
    constexpr float kMeasuredTrimDb[30] = {
          2.9f,   3.8f,   5.0f,   3.3f,   2.3f,   6.3f,   3.6f,   4.0f,   2.3f,   8.1f,
          3.3f,   2.3f,   1.5f,   2.4f,   2.6f,   3.1f,   3.0f,   3.8f,   1.4f,   3.9f,
          2.6f,   3.8f,   7.1f,  -0.8f,   8.4f,   6.2f,   8.4f,   1.7f,   7.8f,  -1.1f
    };

    float measuredLoudnessTrimDb (int key) noexcept
    {
        const float x = clampf ((float) (key - 21) / 3.0f, 0.0f, 29.0f);
        const int i = std::min ((int) x, 28);
        return lerpf (kMeasuredTrimDb[i], kMeasuredTrimDb[i + 1], x - (float) i);
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
    PianoHybrid::instance(); // parse the measured tables here, never on the audio thread
    bodyNoiseScale = std::sqrt (fs / 48000.0f); // keep the rumble's level independent of fs
    rng.setSeed (seed);
    lfo.reset (seed * 2654435761u + 17u);
    // Room for the slowest felt pulse (~60 ms) plus 1 ms jitter, the 40 ms multipath span and a tail.
    maxExcitation = (int) std::ceil (0.06f * fs);
    maxForce = maxExcitation + (int) std::ceil (0.045f * fs) + 2;
    force.assign ((size_t) maxForce, 0.0f);
    scratch.assign ((size_t) maxForce, 0.0f);
    crackForce.assign ((size_t) maxForce, 0.0f);
    crackScratch.assign ((size_t) maxForce, 0.0f);

    std::fill (std::begin (re), std::end (re), 0.0f);
    std::fill (std::begin (im), std::end (im), 0.0f);
    numModes = 0;
    forceLength = forcePos = 0;
    active = damping = stealing = false;
    thump = Thump{};
    residual = Residual{};
    roll = ThunderRoll{};
    body.reset();
    bodyGain = bodyStep = thunderNow = 0.0f;
    thunderMakeup = windSwell = 1.0f;
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
        thunderMakeup = windSwell = 1.0f;
        bodyGain = bodyStep = 0.0f;
        body.reset();
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
    // String. I <-> II: the measured grand (I) morphs, partial by partial, into a Rhodes-style tine (II).
    const float c = clampf (s.character, 0.0f, 1.0f);
    character = c;

    fundamentalHz = midiToHz ((float) midiKey, s.a4Hz) * std::exp2 (stretchCents (k) * (1.0f - c) / 1200.0f);
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
    // I: the felt. II: a soft neoprene tip that leaves the tine's few harmonics alone; its
    // brightness comes from the pickup "bark" below instead.
    const float feltRef  = 580.0f * std::exp2 (0.6f * (float) (k - 60) / 12.0f);
    const float tipRef   = 1800.0f * std::exp2 (0.15f * (float) (k - 60) / 12.0f); // soft neoprene, barely brighter up top
    const float morph = std::pow (tipRef / feltRef, c);
    const float speedFactor = std::pow (hammerSpeed / referenceSpeed, 0.6f * (1.0f - 0.6f * c));
    const float cornerRef = feltRef * morph * std::exp2 ((hardness - 0.5f) * 1.6f * (1.0f - 0.6f * c));
    const float corner = cornerRef * speedFactor;
    const float cornerMeasured = feltRef * morph * speedFactor; // default hardness: the recorded hammer's own
    const float order = lerpf (2.6f - 0.9f * v, 2.4f, c);
    const float orderRef = lerpf (2.6f - 0.9f * 0.63f, 2.4f, c);
    const float impulse = v * (s.softPedal ? 0.75f : 1.0f);

    auto pulseShape = [order] (float hz, float cornerHz) noexcept
    {
        return std::pow (1.0f + (hz / cornerHz) * (hz / cornerHz), -0.5f * order);
    };

    const float decayScale = std::max (s.decayScale, 0.05f);

    // I without measured data (fallback): the analytic stiff string.
    const float sigma1 = 6.91f / (promptT60 (k) * decayScale);
    const float b3 = 2.6e-7f / decayScale;
    const float afterDecaySynth = 0.22f;
    const float afterLevelSynth = s.softPedal ? 0.5f : 0.32f;
    const float unisonSynth = std::max (s.unisonCents, 0.0f) * (k < 32 ? 0.5f : 1.0f) * (0.6f + 0.8f * hash01 ((uint32_t) k, 7));

    // II: a tine sustains long and smooth; its pickup harmonics ("bark") fade into a near-sine.
    const float tineT60 = 10.0f * std::pow (0.25f, kNorm) * decayScale;    // ~10 s bass .. 2.5 s C8
    const float tineSigma1 = 6.91f / tineT60;
    // Bark (pickup harmonics) thins toward the treble, so high notes stay round.
    const float bark = clampf ((0.16f + 0.5f * std::pow (v, 1.5f) + 0.12f * (1.0f - kNorm)) * (1.0f - 0.45f * kNorm), 0.0f, 0.8f);
    const float bellRatio = 6.9f;                                       // tine overtone: an inharmonic "ping"
    const float bellHzNominal = bellRatio * fundamentalHz;
    const float bellLevel = (0.10f + 0.22f * v) * (1.0f - 0.75f * kNorm)
                          / std::sqrt (1.0f + (bellHzNominal / 3500.0f) * (bellHzNominal / 3500.0f));
    constexpr float tineAfterLevel = 0.18f, tineAfterDecay = 0.75f;

    // I: the measured partials of this key at this velocity. The data's level is unit energy at
    // V = 80; 0.63 (the mezzo-forte impulse) puts it level with the synthetic spectra.
    const auto& hybrid = PianoHybrid::instance();
    const bool hasData = hybrid.isValid() && c < 1.0f;
    if (hasData)
        hybrid.partials (k, midiVelocity, measured);
    const int measuredCount = hasData ? measured.count : 0;
    constexpr float kMeasuredLevel = 0.63f;
    const float unisonScale = std::max (s.unisonCents, 0.0f) / 1.2f; // 1.2 cents (default) = as recorded
    const float unaCorda = s.softPedal ? 1.5f : 1.0f;                // one string struck: more aftersound

    const float previousCentre = lastCentre, previousEmphasis = lastEmphasis, previousTilt = lastTilt;

    // Unit-energy references for the synthetic spectra (the tine, and I's fallback), normalised
    // against the mezzo-forte hammer spectrum of this key.
    float acousticEnergy = 0.0f, tineEnergy = 0.0f;
    for (int n = 1; n <= kMaxPartials; ++n)
    {
        const float fn = (float) n * f0 * std::sqrt (1.0f + B * (float) (n * n));
        if (fn > fMax) break;
        const float comb = 0.03f + 0.97f * std::abs (std::sin ((float) n * kPi * strikePoint));
        const float r2 = (fn / 90.0f) * (fn / 90.0f);
        const float a = comb * (r2 / (1.0f + r2)) / std::sqrt (1.0f + (fn / 6000.0f) * (fn / 6000.0f))
                      * std::pow (1.0f + (fn / feltRef) * (fn / feltRef), -0.5f * (2.6f - 0.9f * 0.63f));
        const float ft = (float) n * midiToHz ((float) midiKey, s.a4Hz);
        const float t = (n == 1 ? 1.0f : std::pow (bark, (float) (n - 1)) / std::sqrt ((float) n)) / std::sqrt (1.0f + (ft / 2000.0f) * (ft / 2000.0f));
        acousticEnergy += a * a;
        tineEnergy += t * t;
    }
    const float acousticNorm = 1.0f / std::sqrt (std::max (acousticEnergy, 1.0e-12f));
    const float tineNorm = 1.0f / std::sqrt (std::max (tineEnergy + bellLevel * bellLevel, 1.0e-12f));

    float refAcoustic = 0.0f, refTine = 0.0f;
    for (int n = 1; n <= kMaxPartials; ++n)
    {
        const float fn = (float) n * f0 * std::sqrt (1.0f + B * (float) (n * n));
        const float fTine = (float) n * fundamentalHz;
        if (fn > fMax && fTine > fMax) break;
        const float comb = 0.03f + 0.97f * std::abs (std::sin ((float) n * kPi * strikePoint));
        const float r2 = (fn / 90.0f) * (fn / 90.0f);
        const float acoustic = comb * (r2 / (1.0f + r2)) / std::sqrt (1.0f + (fn / 6000.0f) * (fn / 6000.0f))
                             * (0.8f + 0.4f * hash01 ((uint32_t) k, 100u + (uint32_t) n)) * acousticNorm;
        const float tineTone = 1.0f / std::sqrt (1.0f + (fTine / 2000.0f) * (fTine / 2000.0f));
        const float tine = (n == 1 ? 1.0f : std::pow (bark, (float) (n - 1)) / std::sqrt ((float) n)) * tineTone * tineNorm;
        const float ra = acoustic * std::pow (1.0f + (fn / cornerRef) * (fn / cornerRef), -0.5f * orderRef);
        const float rt = tine * std::pow (1.0f + (fTine / cornerRef) * (fTine / cornerRef), -0.5f * orderRef)
                       + (n == 7 ? bellLevel * tineNorm : 0.0f);
        refAcoustic += ra * ra;
        refTine += rt * rt;
    }
    const float normAcoustic = 1.0f / std::sqrt (std::max (refAcoustic, 1.0e-9f));
    const float normTine = 1.0f / std::sqrt (std::max (refTine, 1.0e-9f));

    float hissAcc = 0.0f, hammerEnergy = 0.0f, crackRef = 0.0f;
    float fundamentalAfterSigma = sigma1 * afterDecaySynth;
    int m = 0;

    for (int n = 1; n <= kMaxPartials; ++n)
    {
        const bool fromData = n <= measuredCount;
        const size_t pn = (size_t) (n - 1);
        const float fSynth = (float) n * f0 * std::sqrt (1.0f + B * (float) (n * n));
        const float fI = fromData ? fundamentalHz * measured.ratio[pn] : fSynth;
        const float fTine = (float) n * fundamentalHz;
        const float fn = fI * std::pow (fTine / fI, c);
        if (fn > fMax)
            break;
        if (hasData && ! fromData && c <= 0.0f)
            break; // past the last measured partial: nothing left to hear

        // Synthetic spectra: I's fallback (stiff string, strike-point comb, soundboard radiation) and
        // II's pickup output. They also say how a raindrop on the string reaches each partial.
        const float comb = 0.03f + 0.97f * std::abs (std::sin ((float) n * kPi * strikePoint));
        const float r2 = (fSynth / 90.0f) * (fSynth / 90.0f);
        const float radiation = (r2 / (1.0f + r2)) / std::sqrt (1.0f + (fSynth / 6000.0f) * (fSynth / 6000.0f));
        const float soundboard = 0.8f + 0.4f * hash01 ((uint32_t) k, 100u + (uint32_t) n);
        const float acousticShape = comb * radiation * soundboard * acousticNorm;
        const float tineTone = 1.0f / std::sqrt (1.0f + (fTine / 2000.0f) * (fTine / 2000.0f)); // the pickup's warm roll-off
        const float tineShape = (n == 1 ? 1.0f : std::pow (bark, (float) (n - 1)) / std::sqrt ((float) n)) * tineTone * tineNorm;
        const float participation = (1.0f - c) * acousticShape + c * tineShape;

        //----------------------------------------------------------------------
        // I: prompt amplitude, aftersound (relative, with phase), decays and frequencies.
        float aI, afterI, phaseI, sigmaIp, sigmaIa, fIp, fIa;
        if (fromData)
        {
            // The recorded hammer, re-voiced by the hardness control (and the una corda) relative to it.
            aI = kMeasuredLevel * measured.amp[pn] * (impulse / v) * pulseShape (fI, corner) / pulseShape (fI, cornerMeasured);
            afterI = measured.after[pn] * unaCorda;
            phaseI = measured.phase[pn];
            sigmaIp = measured.sigma1[pn] / decayScale;
            sigmaIa = measured.sigma2[pn] / decayScale;
            fIp = fI;
            fIa = std::max (fI + measured.beatHz[pn] * unisonScale, 1.0f);
        }
        else
        {
            aI = hasData ? 0.0f : acousticShape * normAcoustic * impulse * pulseShape (fI, corner);
            afterI = afterLevelSynth;
            phaseI = 0.0f;
            sigmaIp = sigma1 + b3 * fI * fI;
            sigmaIa = sigmaIp * afterDecaySynth;
            const float cents = unisonSynth * (0.75f + 0.5f * hash01 ((uint32_t) k, 300u + (uint32_t) n));
            fIp = fI * std::exp2 (cents / 2400.0f);
            fIa = fI * std::exp2 (-cents / 2400.0f);
        }
        if (n == 1)
            fundamentalAfterSigma = sigmaIa * std::pow ((tineSigma1 * tineAfterDecay) / sigmaIa, c);

        // II: the tine.
        const float aII = tineShape * normTine * impulse * pulseShape (fTine, corner);
        const float sigmaII = tineSigma1 * (1.0f + 0.9f * (float) (n - 1));
        const float centsII = 0.35f * (0.75f + 0.5f * hash01 ((uint32_t) k, 300u + (uint32_t) n));

        // Morph: amplitudes linearly (the aftersound as a complex amplitude), frequencies and decay
        // rates geometrically.
        float modeFreq[2]  = { fIp * std::pow (fTine * std::exp2 (centsII / 2400.0f) / fIp, c),
                               fIa * std::pow (fTine * std::exp2 (-centsII / 2400.0f) / fIa, c) };
        float modeSigma[2] = { sigmaIp * std::pow (sigmaII / sigmaIp, c),
                               sigmaIa * std::pow (sigmaII * tineAfterDecay / sigmaIa, c) };
        float ampRe[2] = { (1.0f - c) * aI, (1.0f - c) * aI * afterI * std::cos (phaseI) + c * aII * tineAfterLevel };
        float ampIm[2] = { 0.0f, (1.0f - c) * aI * afterI * std::sin (phaseI) };
        ampRe[0] += c * aII;

        if (n == 7 && c > 0.0f)
        {
            // The tine's own overtone: a short, slightly inharmonic bell at the attack.
            const float bellHz = bellRatio * fundamentalHz;
            if (bellHz < fMax)
            {
                modeFreq[0]  = modeFreq[0] * std::pow (bellHz / modeFreq[0], c);
                modeSigma[0] = modeSigma[0] * std::pow ((6.91f / 0.35f) / modeSigma[0], c);
                ampRe[0] = (1.0f - c) * aI + c * bellLevel * tineNorm * normTine * impulse * pulseShape (bellHz, corner);
            }
        }

        // Rain hiss excites the middle partials (300 Hz .. 2 kHz) most: rain is not a bass sound,
        // and exciting the top partials made it ring like gravel on glass.
        const float rainShape = (fn / 300.0f) / std::sqrt (1.0f + (fn / 300.0f) * (fn / 300.0f))
                              / (1.0f + (fn / 2000.0f) * (fn / 2000.0f));
        const float afterParticipation = (1.0f - c) * afterLevelSynth + c * tineAfterLevel;
        const float crackReference = participation * std::pow (1.0f + (fn / cornerRef) * (fn / cornerRef), -0.5f * orderRef);
        crackRef += crackReference * crackReference;

        for (int j = 0; j < kModesPerPartial; ++j, ++m)
        {
            // The force delivers impulse x pulseShape(f) at each mode's frequency: the drive is the
            // target amplitude divided by it, so the mode rings at exactly the measured (or synthetic)
            // level, darkened by the air.
            const float absorption = atmos::absorptionGain (modeFreq[j], absorbingDistance);
            const float delivered = std::max (impulse * pulseShape (modeFreq[j], corner), 1.0e-6f);
            const float w = kTwoPi * modeFreq[j] / fs;
            const float rho = std::exp (-modeSigma[j] / fs);
            omega[m]   = w;
            zr0[m]     = rho * std::cos (w);
            zi0[m]     = rho * std::sin (w);
            drive[m]   = ampRe[j] * absorption / delivered;
            driveIm[m] = ampIm[j] * absorption / delivered;
            modeHz[m]  = modeFreq[j];
            octaves[m] = std::log2 (modeFreq[j] / fundamentalHz);
            weight[m]  = 1.0f;
            hammerEnergy += (ampRe[j] * ampRe[j] + ampIm[j] * ampIm[j]) * absorption * absorption;

            // A drop (or the rain hiss, or the crack) reaches each mode by its shape on the string.
            const float share = participation * (j == 0 ? 1.0f : afterParticipation);
            partShape[m] = share * absorption;
            crackDrive[m] = share * absorption;
            // Unit-variance noise then settles each mode at |share x rainShape|, whatever its Q.
            hissDrive[m] = share * rainShape * std::sqrt (std::max (1.0f - rho * rho, 0.0f));
            hissAcc += (share * rainShape) * (share * rainShape);
        }
    }

    numModes = ((m + kLanes - 1) / kLanes) * kLanes;

    for (int i = m; i < kMaxModes; ++i)
        omega[i] = zr0[i] = zi0[i] = drive[i] = driveIm[i] = partShape[i] = crackDrive[i] = hissDrive[i] = modeHz[i] = octaves[i] = weight[i] = re[i] = im[i] = 0.0f;

    // The crack excites the modes by their shape, at the level it always had against a mezzo-forte
    // felt pulse (normalised like the synthetic spectra).
    const float crackNorm = 1.0f / std::sqrt (std::max (crackRef, 1.0e-9f));
    for (int i = 0; i < m; ++i)
        crackDrive[i] *= crackNorm;

    // Reference envelope for fused rain: strikes add energy; it never reads the live string.
    referenceDecay = 2.0f * fundamentalAfterSigma; // energy decay rate of the slowest mode
    strikeEnergy = hammerEnergy;
    referenceEnergy = (continuing ? referenceEnergy : 0.0f) + strikeEnergy;
    const float hissNorm = 1.0f / std::sqrt (std::max (hissAcc, 1.0e-9f)); // total steady hiss energy = hissAmp^2
    for (int i = 0; i < m; ++i)
        hissDrive[i] *= hissNorm;

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
        updateWeights (previousCentre, previousEmphasis, previousTilt, lastThunderHz);
    }
    else
    {
        lastCentre = lastEmphasis = -1.0f;
        lastTilt = lastThunderHz = 0.0f;
    }

    //==========================================================================
    // The hammer-string contact, lightning crack and multipath included.
    buildForce (s, kNorm, corner, order, impulse, strikePoint);

    // Tension modulation: loud notes start slightly sharp and settle.
    glideDepth = 0.0011f * v * v;
    timeSinceStrike = 0.0f;

    // I: the strike's own noise, measured - hammer, action and soundboard, re-pitched to this key and
    // darkened by distance like the strike.
    residual = Residual{};
    if (hasData)
    {
        PianoHybrid::Residual r;
        hybrid.residual (k, midiVelocity, r);
        const float g = kMeasuredLevel * (1.0f - c) * (s.softPedal ? 0.8f : 1.0f);
        residual.data[0] = r.data[0];
        residual.data[1] = r.data[1];
        residual.gain[0] = r.gain[0] * g;
        residual.gain[1] = r.gain[1] * g;
        residual.length = r.length;
        residual.inc = (fundamentalHz / std::max (r.sourceF0, 1.0f)) * (r.sourceRate / fs);
        residual.delay = thump.delay;
        const float dark = std::min (atmos::absorptionCascadeCutoff (absorbingDistance, 2), 0.45f * fs);
        residual.lp1.setCutoff (dark, fs);
        residual.lp2.setCutoff (dark, fs);
        residual.active = r.data[0] != nullptr && r.length > 1 && (residual.gain[0] > 0.0f || residual.gain[1] > 0.0f);
    }

    // II's soundboard knock (I's is in the measured residual).
    {
        const float knock = std::max (s.crackLevel, 0.0f) * impulse * 0.3f * (1.0f - 0.6f * c) * (hasData ? c : 1.0f);
        const float tauThump = 0.035f * (1.0f - 0.5f * kNorm);
        const float thumpHz = 70.0f + 90.0f * kNorm;
        const float rho = std::exp (-1.0f / (tauThump * fs));
        thump.zr = rho * std::cos (kTwoPi * thumpHz / fs);
        thump.zi = rho * std::sin (kTwoPi * thumpHz / fs);
        thump.re += knock;
        thump.samplesLeft = (int) (fs * 7.0f * tauThump);
        thump.active = knock > 0.0f;
    }

    //==========================================================================
    // The thunder rolls through the low strings: a clap right after the strike, then a few
    // irregular swells over the roll time - longer and darker for a distant strike.
    roll = ThunderRoll{};
    roll.depth = clampf (1.6f * thunderKeyWeight (k) * clampf (s.rollDepth, 0.0f, 1.0f) * (0.6f + 0.4f * d), 0.0f, 1.0f);
    if (roll.depth > 0.0f)
    {
        const float rollTime = std::max (s.rollSeconds, 0.3f) * (0.5f + 0.5f * d);
        roll.count = 2 + (int) (rng.uniform() * 3.0f); // 2..4 swells
        float t = 0.03f + 0.07f * rng.uniform();
        for (int i = 0; i < roll.count; ++i)
        {
            roll.time[i] = t;
            roll.width[i] = (0.12f + 0.25f * rng.uniform()) * (0.7f + 0.3f * d);
            roll.height[i] = (i == 0 ? 1.0f : (0.55f + 0.45f * rng.uniform()) * std::exp (-0.5f * t / rollTime));
            t += rollTime / (float) roll.count * (0.6f + 0.8f * rng.uniform());
        }
    }
    thunderNow = 0.0f;
    // The body's rumble sits in thunder's register, under the note.
    body.set (55.0f + 35.0f * kNorm, 0.9f, fs);
    bodyRef = std::sqrt (strikeEnergy);

    // II's own balance: a tine's bass lives in its fundamental, and its treble is a near-sine at
    // 2-4 kHz where the ear is most sensitive - level-matched by meter it sounds piercing, so the
    // top is trimmed by ear (about the ISO 226 equal-loudness difference) rather than by RMS.
    const float tineTrimDb = 9.0f * std::max (0.0f, 0.45f - kNorm) / 0.45f
                           - 10.0f * clampf ((kNorm - 0.45f) / 0.55f, 0.0f, 1.0f);

    // II's attack swells in over a millisecond or few (longer up top) instead of clicking on.
    const float attackSeconds = c * (0.0008f + 0.0035f * kNorm);
    attackCoef = attackSeconds > 0.0f ? 1.0f - std::exp (-1.0f / (attackSeconds * fs)) : 1.0f;
    if (! continuing)
        attackGain = attackSeconds > 0.0f ? 0.0f : 1.0f;
    const float trimI = hasData ? measuredLoudnessTrimDb (k) : gainToDb (keyLoudnessTrim (kNorm));
    outputGain = 0.1f * dbToGain ((1.0f - c) * trimI + c * (gainToDb (keyLoudnessTrim (kNorm)) + tineTrimDb));
    env = 1.0f;
    damping = false;
    stealing = false;
    stealGain = 1.0f;
    dampCoef = 1.0f;
    energy = std::max ((continuing ? energy : 0.0f) + strikeEnergy, 1.0e-6f);
    active = true;
}

float PianoVoice::thunderKeyWeight (int midiKey) noexcept
{
    // Nothing from middle C up; below it the roll grows toward A0, where the note's partials sit in
    // thunder's own register (rumble energy peaks around 50-150 Hz).
    const float below = clampf ((60.0f - (float) midiKey) / 39.0f, 0.0f, 1.0f);
    return std::pow (below, 1.5f);
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
    std::fill (crackScratch.begin(), crackScratch.begin() + excitationLength, 0.0f);

    // Crack: broadband, ~10 dB under the pulse's low-frequency content, so it rules the upper partials.
    const float crack = std::max (s.crackLevel, 0.0f);
    if (crack > 0.0f)
    {
        // Harder strikes crack more: the felt stiffens and the contact turns noisy.
        // I: a touch gentler than a raw model; II: a neoprene tip barely cracks at all.
        const float amp = crack * 0.2f * (1.0f - 0.9f * character) * impulse * std::pow (impulse, 1.5f)
                        / std::sqrt (0.5f * tauCrack * fs);
        const float decay = std::exp (-1.0f / (tauCrack * fs));
        float e = amp;
        for (int i = 0; i < crackLength; ++i)
        {
            crackScratch[i] = e * rng.bipolar() * 1.7320508f;
            e *= decay;
        }
    }

    //--------------------------------------------------------------------------
    // h(t): multipath. The pulse returns from the near termination after one round trip
    // (strike point -> agraffe -> strike point = strikePoint / f1) and re-contacts the felt.
    // Close strikes stay a single localised contact; distant ones roll through several delayed,
    // progressively darker contacts with irregular spacing - the time-varying delay distribution.
    const float depth = clampf (s.multipath, 0.0f, 1.0f) * d;
    const int taps = 1 + (int) std::lround (4.0f * depth);
    const float roundTrip = strikePoint / std::max (fundamentalHz, 1.0f);
    const float spacing = roundTrip + depth * 0.002f;
    const float maxSpan = 0.02f * fs;

    float gains[8] {}, delays[8] {}, cutoffs[8] {};
    float gainSum = 0.0f, g = 1.0f, t = 0.0f;
    const float ratio = 0.4f + 0.25f * depth; // later contacts fade fast: a soft roll, not a flam
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
    std::fill (crackForce.begin(), crackForce.begin() + forceLength, 0.0f);

    for (int tap = 0; tap < taps && gains[tap] > 0.0f; ++tap)
    {
        const int offset = jitter + (int) delays[tap];
        const float gain = gains[tap] / gainSum;
        OnePoleLP darken, darkenCrack;
        darken.setCutoff (tap == 0 ? 1.0e9f : cutoffs[tap], fs);
        darkenCrack.setCutoff (tap == 0 ? 1.0e9f : cutoffs[tap], fs);
        for (int i = 0; offset + i < forceLength; ++i)
        {
            const bool inside = i < excitationLength;
            force[offset + i] += gain * darken.process (inside ? scratch[i] : 0.0f);
            crackForce[offset + i] += gain * darkenCrack.process (inside ? crackScratch[i] : 0.0f);
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
    residual = Residual{};
    roll = ThunderRoll{};
    bodyGain = bodyStep = thunderNow = 0.0f;
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

void PianoVoice::updateWeights (float centreHz, float emphasis, float tilt, float thunderHz) noexcept
{
    constexpr float q = 1.6f;
    const float makeup = 1.0f / (1.0f + 0.3f * emphasis);
    const float centre = std::max (centreHz, 1.0f);
    const bool thunder = thunderHz > 0.0f;

    // The thunder's low-pass is loudness-compensated against what the string holds right now, so
    // it changes the colour and barely the level (the makeup restores 90 % of what it takes).
    float open = 0.0f, filtered = 0.0f;
    for (int i = 0; i < numModes; ++i)
    {
        if (modeHz[i] <= 0.0f)
        {
            weight[i] = 0.0f;
            continue;
        }
        const float brightness = tilt == 0.0f ? 1.0f : std::exp2 (tilt * octaves[i]);
        const float w = (1.0f + emphasis * bandPassMagnitude (modeHz[i], centre, q)) * makeup * brightness;
        if (thunder)
        {
            const float r2 = (modeHz[i] / thunderHz) * (modeHz[i] / thunderHz);
            const float lp = 1.0f / std::sqrt (1.0f + r2 * r2); // two poles: 12 dB per octave
            const float e = (re[i] * re[i] + im[i] * im[i]) * w * w;
            open += e;
            filtered += e * lp * lp;
            weight[i] = w * lp;
        }
        else
        {
            weight[i] = w;
        }
    }
    thunderMakeup = (thunder && filtered > 1.0e-20f) ? clampf (std::pow (open / filtered, 0.45f), 1.0f, 4.0f) : 1.0f;
    lastCentre = centreHz;
    lastEmphasis = emphasis;
    lastTilt = tilt;
    lastThunderHz = thunderHz;
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

    // The drop's impact spectrum, imprinted on the string. A drop is a soft impactor on a wound
    // string, so it taps the warm partials around a third of its free-field centre (~1 kHz) rather
    // than pinging the top ones. Each drop adds amount^2 of the strike's reference energy, so the
    // texture follows the note without feeding on itself.
    const float centre = std::max (0.3f * impactCentreHz, 100.0f);
    float shapeEnergy = 0.0f;
    for (int i = 0; i < numModes; ++i)
    {
        const float hz = std::max (modeHz[i], 1.0f);
        const float g = partShape[i] * bandPassMagnitude (hz, centre, 0.6f) / (1.0f + (hz / 2500.0f) * (hz / 2500.0f));
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

    //==========================================================================
    // Thunder rolling through the low strings: each swell r(t) = sum a_k (u/tau_k) e^(1 - u/tau_k),
    // u = t - t_k, sweeps a two-pole low-pass from 6 octaves above the fundamental (open) down
    // toward 2.8 f1 at full depth - for the lowest keys, thunder's own register - and lowers the
    // pitch by up to 8 cents. Colour first; level only through the 10 % the loudness makeup leaves
    // and a slight swell.
    float rollNow = 0.0f;
    for (int i = 0; i < roll.count; ++i)
    {
        const float u = timeSinceStrike - roll.time[i];
        if (u > 0.0f)
        {
            const float x = u / roll.width[i];
            rollNow += roll.height[i] * x * std::exp (1.0f - x);
        }
    }
    thunderNow = clampf (roll.depth * std::min (rollNow, 1.2f), 0.0f, 1.0f);
    const float thunderHz = thunderNow > 0.002f ? fundamentalHz * std::exp2 (6.0f - 4.5f * thunderNow) : 0.0f;
    const float thunderPitch = std::exp2 (-8.0f * thunderNow / 1200.0f);

    const float ratio = std::exp (pitchDepth * lnLocal) * glide * thunderPitch;

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

    // While the thunder rolls the weights follow it (and its makeup follows the string) every tick.
    if (lastCentre < 0.0f || std::abs (centre - lastCentre) > 0.004f * lastCentre
        || std::abs (emphasis - lastEmphasis) > 0.01f || std::abs (tilt - lastTilt) > 0.005f
        || thunderHz > 0.0f || lastThunderHz > 0.0f)
        updateWeights (centre, emphasis, tilt, thunderHz);

    windSwell = 1.0f + 0.15f * timbre * std::min (strength, 1.0f) * clampf (sweep, -1.5f, 1.5f);
    swellTarget = windSwell * thunderMakeup * (1.0f + 0.1f * thunderNow);
    swellStep = (swellTarget - swellGain) / std::max (1.0f, dt * fs);

    // The body rumbles under the roll, in thunder's register.
    const float bodyTarget = kBodyRumble * thunderNow * bodyRef;
    bodyStep = (bodyTarget - bodyGain) / std::max (1.0f, dt * fs);

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

    const bool rolling = thunderNow > 0.0f || bodyGain > 1.0e-9f;
    if (contactOver && ! thump.active && ! residual.active && ! rolling && energy < 1.0e-10f)
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

    // Even lanes hold prompt modes, odd lanes aftersound modes: their difference is a stereo "side"
    // that wanders as the two beat, like a piano recorded with a spaced pair.
    const float prompt = (acc[0] + acc[2]) + (acc[4] + acc[6]);
    const float after  = (acc[1] + acc[3]) + (acc[5] + acc[7]);
    lastSide = prompt - after;
    return prompt + after;
}

float PianoVoice::tickModesDriven (float f, float fc) noexcept
{
    float acc[kLanes] {};

    for (int base = 0; base < numModes; base += kLanes)
    {
        for (int j = 0; j < kLanes; ++j)
        {
            const int i = base + j;
            const float r = re[i], q = im[i];
            const float nr = r * zr[i] - q * zi[i] + f * drive[i] + fc * crackDrive[i];
            const float nq = r * zi[i] + q * zr[i] + f * driveIm[i];
            re[i] = nr;
            im[i] = nq;
            acc[j] += nq * weight[i];
        }
    }

    // Even lanes hold prompt modes, odd lanes aftersound modes: their difference is a stereo "side"
    // that wanders as the two beat, like a piano recorded with a spaced pair.
    const float prompt = (acc[0] + acc[2]) + (acc[4] + acc[6]);
    const float after  = (acc[1] + acc[3]) + (acc[5] + acc[7]);
    lastSide = prompt - after;
    return prompt + after;
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

float PianoVoice::tickResidual() noexcept
{
    auto& r = residual;
    if (r.delay > 0)
    {
        --r.delay;
        return 0.0f;
    }

    const int i0 = (int) r.pos;
    if (i0 + 1 >= r.length)
    {
        r.active = false;
        return 0.0f;
    }
    const float frac = r.pos - (float) i0;
    float x = 0.0f;
    for (int layer = 0; layer < 2; ++layer)
    {
        if (r.gain[layer] == 0.0f)
            continue;
        const float a = PianoHybrid::decode (r.data[layer][i0]);
        const float b = PianoHybrid::decode (r.data[layer][i0 + 1]);
        x += (a + frac * (b - a)) * r.gain[layer];
    }
    r.pos += r.inc;
    return r.lp2.process (r.lp1.process (x));
}

void PianoVoice::render (float* out, int numSamples) noexcept
{
    render (out, nullptr, numSamples);
}

void PianoVoice::render (float* out, float* side, int numSamples) noexcept
{
    if (! active)
    {
        std::fill (out, out + numSamples, 0.0f);
        if (side != nullptr)
            std::fill (side, side + numSamples, 0.0f);
        return;
    }

    // Stereo width of the mode pair: a little for the grand (I), more for the tine (II).
    const float width = 0.22f + 0.33f * character;

    for (int i = 0; i < numSamples; ++i)
    {
        float y0;
        if (forcePos < forceLength)
        {
            y0 = tickModesDriven (force[(size_t) forcePos], crackForce[(size_t) forcePos]);
            ++forcePos;
        }
        else
        {
            y0 = tickModes();
        }
        float y = y0;

        if ((swellStep > 0.0f && swellGain < swellTarget) || (swellStep < 0.0f && swellGain > swellTarget))
            swellGain += swellStep;

        if (damping)
            env *= dampCoef;

        attackGain += (1.0f - attackGain) * attackCoef;
        const float gain = swellGain * env * outputGain * attackGain;
        y *= swellGain * env * attackGain;
        float sd = lastSide * gain * width;

        if (thump.active)
            y += tickThump();

        if (residual.active)
            y += tickResidual();

        if (bodyGain > 1.0e-9f || bodyStep > 0.0f)
        {
            body.process (rng.bipolar() * 1.7320508f * bodyNoiseScale);
            y += body.lp * bodyGain;
        }
        bodyGain = std::max (bodyGain + bodyStep, 0.0f);

        y *= outputGain;

        if (stealing)
        {
            stealGain -= stealStep;
            if (stealGain <= 0.0f)
            {
                std::fill (out + i, out + numSamples, 0.0f);
                if (side != nullptr)
                    std::fill (side + i, side + numSamples, 0.0f);
                active = false;
                stealing = false;
                std::fill (std::begin (re), std::end (re), 0.0f);
                std::fill (std::begin (im), std::end (im), 0.0f);
                thump = Thump{};
                residual = Residual{};
                roll = ThunderRoll{};
                bodyGain = bodyStep = thunderNow = 0.0f;
                forceLength = forcePos = 0;
                hissAmp = referenceEnergy = energy = 0.0f;
                return;
            }
            y *= stealGain;
            sd *= stealGain;
        }

        out[i] = y;
        if (side != nullptr)
            side[i] = sd;
    }
}

} // namespace petrichor
