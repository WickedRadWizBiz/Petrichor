# Petrichor Piano

Petrichor Piano is a modal piano played inside a storm. Each key is a string built from a bank of modal resonators: for Piano I, a real grand (the Salamander Grand Piano, a Yamaha C5) analysed partial by partial and resynthesised; for Piano II, a Rhodes-style tine. The storm is made of the same physics:

- **Thunder is the hammer strike, and a roll in the low strings.** How hard you play sets how far away the lightning is. A fortissimo is one bright contact overhead; a pianissimo reaches the string muffled by the air. Below middle C the thunder then rolls through the string: a few swells that darken the note toward thunder's register and lower it by a few cents, deepest toward A0. It is colour, not volume, and there is no separate thunder sound: you hear it through the string.
- **Wind** either blows beside the piano (*overlay*: roar and whistling wires) or *fuses* into the notes, bending each note's pitch and timbre the way gusts bend a whistle's pitch, with each key moving at its own Strouhal tempo.
- **Rain** either falls beside the piano as a granular texture that listens to your playing and to the wind (*overlay*), or *fuses* into the sound: drops land on the ringing strings and the patter textures each note from inside.

It builds as a VST3, AU and Standalone instrument (JUCE 8) with an offline React UI. The sound comes from a JUCE-free C++17 engine, so you can test it and render audio from the command line without a host.

- 64 voices, each with up to 96 partials and 2 modes per partial. Voices are stolen by kinetic energy.
- Events are sample accurate, because the plug-in splits every block at MIDI timestamps. Modulation runs at a 32-sample control rate.
- After `prepare()`, the engine is real-time safe and deterministic: it allocates nothing, takes no locks, and uses fixed seeds.

### Overlay and Fuse

Wind and Rain each have an **Overlay ↔ Fuse** slider. The blend is equal-power: overlay gain $\cos(b\pi/2)$, fuse amount $\sin(b\pi/2)$.

| | Overlay (left) | Fuse (right) |
|---|---|---|
| **Wind** | The wind is its own sound beside the piano: a roar that follows dynamic pressure ($\propto U^2$) and three wires whistling at $f = St\,U/D$ (`wind_air`). The notes are untouched. | The wind lives inside the notes: each note's pitch follows the local wind like an Aeolian tone ($f \propto U$), gusts tilt its brightness and sweep a resonance through its partials (`wind_pitch`, `wind_timbre`). |
| **Rain** | A texturiser-style layer beside the piano: Marshall-Palmer grains driven by the wind's gusts *and* by the piano: louder playing brings denser, louder drops whose tone leans toward the piano's (`rain_follow`). | A texture modulation inside the piano sound: drops land on the ringing strings and sound in the chord's own partials, a rain hiss excites the strings' modes, and the drop patter modulates the tone grain by grain. Silent when nothing is sounding. |
| **Piano I ↔ II** | I: a real grand, resynthesised: the Salamander Yamaha C5's measured partials, decays, beats and hammer noise, with sympathetic string resonance | II: a Rhodes-style tine piano: harmonic partials with a round fundamental, a bell ping at the attack, pickup "bark" when struck hard, a soft tip and no crack |
| **Thunder** | (no slider: always fused) | The strike is the lightning: velocity is distance, and the crack and the air absorption happen in the hammer-string contact. Below middle C the thunder rolls through the low strings as a moving low-pass and a slight pitch sag. |

---

## Signal flow

```text
 MIDI in (all channels)          on-screen keyboard (UI -> native noteOn / noteOff)
            \                       /
             v                     v
 PluginProcessor::processBlock   merges keyboard events, splits the block at every MIDI timestamp
             |
             v
 PetrichorEngine::process        control tick every 32 samples
 +-------------------------------------------------------------------------------------------+
 | StormWind     U(t) = U (1 + I G(t)),  G = Kolmogorov noise, corner U / (8.41 L)           |
 |   |  WindState { meanSpeed, speed, gust, gustFactor, intensity }                          |
 |   +--> RainTexture.beginBlock   R(t) -> Lambda -> flux -> Poisson drop events             |
 |   |        fused drops ------------------------> land on strings (energy-weighted)        |
 |   +--> PianoVoice x 64      modal string: I = measured partials (PianoHybrid), II = tine  |
 |   |      force = (gamma felt pulse + crack) * re-contacts h(t), exp(-alpha f^2 x) per mode |
 |   |      I: + measured attack residual (hammer, action, soundboard noise)                 |
 |   |      thunder roll (below C4): low-pass sweep + pitch sag + body rumble                |
 |   |      fused wind: pitch (U_key/U)^depth, brightness tilt, band-pass weights            |
 |   |      fused rain: drop impulses + hiss into the modes (open-loop strike reference)     |
 |   |        +-- equal-power key pan (bass left) -----------------------> piano bus         |
 |   +--> SympatheticResonance free strings ring along (I) ------------------> piano bus     |
 |   +--> RainTexture.render   overlay grains (+ piano follow) -> dry bus; patter signal     |
 |   |      piano bus x (1 + depth * patter)  (fused rain AM) ---------------> dry bus       |
 |   +--> WindAir              overlay: roar ~ U^2, Aeolian wire whistles at St U / D        |
 |                                   sum -> master gain -> soft knee above 0.8 -> L / R      |
 +-------------------------------------------------------------------------------------------+
             |                                   |
             v                                   v
        host audio out             telemetry (atomics) -> editor timer, 30 Hz -> UI event
```

`d = (127 - V) / 126` is the strike's distance fraction: 0 when the strike is overhead and 1 at the storm's edge.

---

## The mathematics

Atmospheric physics lives in `Source/DSP/Atmosphere.h` (namespace `petrichor::atmos`). The modules call those functions instead of re-deriving the formulas. Every constant quoted below is the value used in the code.

### Rain

#### Marshall-Palmer drop size distribution

$$
N(D) = N_0\, e^{-\Lambda D}, \qquad N_0 = 8000\ \mathrm{m^{-3}\,mm^{-1}}, \qquad \Lambda = 4.1\, R^{-0.21}\ \mathrm{mm^{-1}}
$$

This gives the number of drops per cubic metre per millimetre of diameter. Heavier rain (larger $R$, in mm/h) flattens the exponential, so big drops become more common.

- **Code:** `atmos::marshallPalmerLambda()` and `atmos::marshallPalmerDensity()`. Diameters are limited to 0.1 to 6 mm (`kMinDropMM` and `kMaxDropMM`), because larger drops break up.
- **Sound:** $\Lambda$ sets both how many grains fire and how large each drop is, as described below. For example, $\Lambda$ is 4.10 at 1 mm/h, 2.65 at 8 mm/h and 1.43 at 150 mm/h.

#### Terminal velocity

$$
v_T(D) = \min\!\left(\sqrt{\frac{4\, g\, \rho_w\, D}{3\, \rho_a\, C_d}},\ 9.3\ \mathrm{m/s}\right)
$$

This is the drag-balanced fall speed of a sphere. The code uses $g = 9.81$, $\rho_w = 1000$, $\rho_a = 1.225$ and $C_d = 0.5$.

- **Code:** `atmos::terminalVelocity()`. For example, $v_T(2\ \mathrm{mm}) = 6.54$ m/s.
- **Approximation:** the pure sphere law overestimates the speed of large, flattened drops. The code caps it at the observed 9.3 m/s (Gunn & Kinzer), which applies from $D \approx 4$ mm upward.

#### Wind advection

$$
\mathbf{v} = \mathbf{v}_T + \mathbf{U}, \qquad \lvert \mathbf{v} \rvert = \sqrt{v_T^2 + U^2}, \qquad \sin\theta = \frac{U}{\lvert \mathbf{v} \rvert}
$$

A drop's trajectory is the vector sum of its fall speed and the horizontal wind, so it lands faster and at a slant.

- **Code:** `atmos::impactSpeed()` and `atmos::slantSine()`, called from `RainTexture::spawnGrain()`.
- **Sound:** the impact speed sets each grain's pitch and level. The slant biases the stereo position downwind: `pan = clamp(u (1 - 0.5 sin θ) + 0.6 sin θ)`, where `u` is uniform in [-1, 1].

#### Drop flux and Poisson arrivals

$$
F(R) = \int_{0.1\,\mathrm{mm}}^{6\,\mathrm{mm}} N(D)\, v_T(D)\, dD \quad [\mathrm{drops\ m^{-2}\,s^{-1}}], \qquad
\lambda_g = F \cdot A_c, \qquad P(k) = \frac{(\lambda_g t)^k e^{-\lambda_g t}}{k!}
$$

The distribution gives a concentration, but what you hear is an arrival rate: drops per second through a surface.

- **Code:** `atmos::dropNumberFlux()` computes the integral with Simpson's rule over 32 intervals. `RainTexture::controlTick()` multiplies the flux by a collector area $A_c$ = `kCollectorArea` = 0.015 m² to get a grain rate $\lambda_g$. `RainTexture::render()` draws exponential inter-arrival gaps, $\Delta t = -\ln u / \lambda_g$, so arrivals form a Poisson process. Because the process is memoryless, the pending gap is rescaled when the rate changes.
- **Sound:** about 50 grains/s at 1 mm/h, 104 at 8 mm/h, 153 at 25 mm/h and 276 at 150 mm/h. At most 512 grains sound at once, and at most 8 are spawned per sample.
- **Beyond the spec:** the spec links density to $N(D)$. The code goes one step further and uses the physically correct arrival flux, $N \cdot v_T$.

#### Flux-weighted drop sampling

$$
p(D) \propto N(D)\, v_T(D), \qquad D \in [0.1, 6]\ \mathrm{mm}
$$

Fast drops reach the ground more often than their concentration suggests. A drop that lands is therefore drawn from the flux-weighted spectrum, not from $N(D)$.

- **Code:** `atmos::sampleImpactDiameter()` uses the inverse CDF of the truncated exponential, then accepts the sample with probability $v_T(D)/9.3$, retrying up to 16 times. At $R = 20$ mm/h the mean landing diameter is 0.73 mm, and the tests check the sampled mean against the analytic value to within 1 %.

#### Wind-coupled rain rate

$$
R(t) = R_0 \Bigl(1 + \min\!\bigl(\tfrac{\bar U}{8\ \mathrm{m/s}}, 1\bigr)\,\bigl(g(t) - 1\bigr)\Bigr)^{3c}, \qquad g(t) = 1 + I\, G(t)
$$

The spec links rainfall rate to the absolute amplitude of the wind LFO. $g(t)$ is the storm's gust factor (see "Storm gusts" under Wind), $R_0$ is `rain_rate`, and $c$ is `rain_coupling`. The $\min(\bar U/8, 1)$ term makes the swell depend on there actually being wind: in still air the rain does not breathe.

- **Code:** `RainTexture::controlTick()`, smoothed with a 0.25 s time constant.
- **Sound:** a gust raises $R$, which lowers $\Lambda$ (bigger drops) and raises the flux (denser grains). The gust also raises $U$, which raises the impact speed and makes the texture brighter. In a lull the rain thins to a sparse, low hiss. The tests require a gust (gust factor 1.5, 15 m/s) to more than double the grain rate of a lull (0.6, 6 m/s) and to raise the spectral centroid by more than 30 %.
- **Design choice:** the exponent $3c$ is not a meteorological law. At full coupling, $R$ scales like $U^3$, the same way wind power does.

#### Impact grains

$$
r = \sqrt{r_0^2 + u\,(r_1^2 - r_0^2)}, \qquad
A \propto \Bigl[(D/2)^{1.5}\,\tfrac{\lvert\mathbf v\rvert}{6.5}\Bigr]^{0.7} \min\!\Bigl(1, \frac{0.8\ \mathrm{m}}{r}\Bigr), \qquad
e(t) = A\,\bigl(e^{-t/\tau} - e^{-t/0.25\,\mathrm{ms}}\bigr), \quad \tau = 0.8\ \mathrm{ms} + 1.8\ \tfrac{\mathrm{ms}}{\mathrm{mm}}\, D
$$

Each drop lands somewhere on the ground around the listener, uniformly by area ($p(r) \propto r$, $r_0$ = 0.4 m to $r_1$ = 15 m), so most drops are far away, quiet and dull, and only a few land close enough to be heard as individual "pats" (after Farnell, *Designing Sound*). Kinetic energy grows as $D^3 v^2$, so the amplitude goes as $D^{1.5} v$, compressed by the 0.7 power. The envelope is a difference of exponentials: it swells in over about 0.25 ms instead of switching on, which is what made earlier grains click like gravel on glass.

- **Code:** `impactCentreHz()` and `RainTexture::spawnGrain()`. Each grain is noise shaped two ways and summed:
  - **Body** (0.8): the struck surface's soft "pat", a state-variable band-pass (Q 2) at $500\,\mathrm{Hz}\cdot(1\,\mathrm{mm}/D)^{0.4}$, jittered ×0.8 to ×1.25: lower for bigger drops.
  - **Tick** ($0.45\,\mathrm{clamp}(\lvert\mathbf v\rvert/6.5, 0.1, 2.5)^{0.8}$): the impact's own noise through two one-pole low-passes at $\min(1200\,\lvert\mathbf v\rvert^{0.7},\ 16\,\mathrm{kHz}/(1 + r/1.5\,\mathrm m))$: brighter for faster drops, darker the farther away.
  
  A grain lasts $9.2\,\tau + 4$ ms. `impactCentreHz()` ($700\,\lvert\mathbf v\rvert^{0.75}$ Hz) still sets each drop event's spectral centre for the fused path.
- **Sound:** the centre is about 1.2 kHz for 2 m/s drizzle, 2.9 kHz for a 2 mm drop in still air, and 5.6 kHz for rain driven at 16 m/s. Compared with the old band-passed clicks, at 8 mm/h in a 6 m/s wind the overlay's spectral centroid fell from 5.0 to 2.0 kHz, the share above 4 kHz from 64 % to 10 %, and the fraction of 1 ms frames louder than three times the RMS (clicks) from 2.7 % to 0.26 %.

#### Minnaert bubbles (beyond the spec)

$$
f_M \approx \frac{3.26\ \mathrm{m \cdot Hz}}{a}
$$

A drop landing in standing water can trap an air bubble, which rings at its Minnaert resonance. Here $a$ is the bubble radius in metres.

- **Code:** `atmos::minnaertFrequency()` and `RainTexture::spawnGrain()`. A drop landing within 5 m gets a bubble with probability `rain_surface` × 0.45 if it is larger than 1 mm, or `rain_surface` × 0.1 if smaller. The bubble swells in over 0.5 ms. The bubble radius is 0.25 to 0.6 × D, its decay is $\tau_b = 8\ \mathrm{ms}\sqrt{2000/f}$, and its pitch chirps upward by 15 to 35 % over $4\tau_b$. A bubble of radius 1 mm rings at 3.26 kHz.

#### Hiss bed

Under the overlay grains sits the distant-rain bed, thousands of far drops merged into one diffuse, decorrelated stereo wash. It carries most of the sound of rain, about 10 dB above the near drops. Its noise is tilted between pink and white (about −1.5 dB/octave: half a Kellet pink filter plus 0.15 white), passed through a state-variable low-pass (Q = 0.6) at $1500\,\bar v^{0.65}$ Hz, where $\bar v$ is the mean impact speed, and then a 120 Hz high-pass. Its gain is $0.025\sqrt{\lambda_g/200}\,\sqrt{\bar v/4}$. A lull therefore gives a quiet, dark hiss and a gust a wide, bright one. The rain is scaled by $3.15\,\ell^{1.5}$, where $\ell$ is `rain_level`. Noise is scaled by $\sqrt{f_s/48\,\mathrm{kHz}}$ so levels do not depend on the sample rate. The code is in `RainTexture::controlTick()` and `RainTexture::render()`.

#### Overlay: following the piano

The overlay layer listens to the piano as well as to the wind. The engine measures the dry piano bus every control tick (`PetrichorEngine::controlTick()`):

$$
e(t) = \frac{\mathrm{RMS}}{0.04}\ \text{(10 ms attack, 300 ms release)}, \qquad
f_\mathrm{piano} = \frac{f_s}{2\pi}\sqrt{\frac{E[\dot x^2]}{E[x^2]}}
$$

With $\phi$ = `rain_follow`, the follow factor is $F = (1 - \phi) + \phi\, \min(e, 1.5)$. Each overlay drop sounds with probability $\min(F, 1)$ and is scaled by $\sqrt{\max(F, 1)}$, the hiss by $\sqrt F$, and every grain's centre and the hiss cutoff lean geometrically toward $f_\mathrm{piano}$ by $0.5\,\phi\,\min(e, 1)$. At full follow the overlay is silent when the piano is, as the tests check. Overlay gain is $\cos(b\pi/2)$ of `rain_blend`.

#### Fuse: rain inside the notes

With fuse amount $\varphi = \sin(b\pi/2)$ and texture amount $\ell$, the same drop stream acts on the piano itself:

- **Drops on strings** (`PetrichorEngine::landDrops()`, `PianoVoice::rainDrop()`): each drop lands on one ringing string, chosen at random with probability $\propto \sqrt{E}$, and adds an impulse to its modes shaped by the drop's impact spectrum: a band-pass (Q 0.6) at 0.3 × the drop's centre frequency (about 1 kHz), times a second-order roll-off above 2.5 kHz, applied to the mode drive. A drop is a soft impactor, so it taps the string's warm partials instead of pinging the top ones. The impulse adds $a^2 E_\mathrm{ref}$ of energy with $a = s_d \sqrt{P / \lambda_g}$, where $s_d = \sqrt{\min(A_d / 0.12, 6.25)}$ is the compressed drop strength and $P = 2\,\varphi\,\ell\,\min(\rho, 2)$ is the injected power per second ($\rho$ the drop-density factor). More rain means more, smaller taps rather than a louder texture.
- **Hiss excitation** (`PianoVoice::controlTick()`): once per control period every mode receives a random-amplitude impulse; scaled by $\sqrt{1 - \rho_m^2}$ per mode, this is white noise that settles each mode at a level set by its drive, so the strings sing a rain hiss in their own partials. The hiss weights the modes by a high-pass at 300 Hz and a second-order low-pass at 2 kHz. The total steady hiss energy is $h^2 E_\mathrm{ref}$ with $h = 0.3\,\varphi\,\ell\,\min(\rho, 2)$.
- **Patter modulation** (`PetrichorEngine::renderBlock()`): the piano bus is multiplied by $\mathrm{clamp}(1 + 1.5\,\varphi\,\ell\, p(t), 0.3, 1.7)$, where $p(t)$ is the sum of the drops' compressed impact envelopes with random polarity, panned like the drops.
- **Why it cannot run away:** $E_\mathrm{ref}$ is an open-loop reference, the energy the hammer is expected to deliver ($\sum_m (\mathrm{drive}_m \lvert F(f_m)\rvert)^2$, computed analytically at the strike), decaying at the string's slowest rate and with the damper. An earlier version scaled the texture by the string's live energy; texture then fed on itself and grew without bound. Fused rain is silent when no string sounds.

### Wind

#### Spatial gust field (fBm), and how the code replaces it

$$
W(x, y, t) = \sum_{i=0}^{N} a^i\, \mathrm{Noise}\!\left(b^i x,\ b^i y,\ c^i t\right)
$$

**Not implemented as a spatial field.** An octave sum with persistence $a = b^{-H}$ is fractional Brownian motion with Hurst exponent $H$. Its spectrum falls as $f^{-(2H+1)}$, and Kolmogorov's −5/3 law corresponds to $H = 1/3$ ($a \approx 0.79$ for $b = 2$).

The code produces that spectrum directly by filtering noise, in time only (see the next section). Space is represented instead by independent, separately seeded realisations:

- one storm-wide gust signal, and
- one turbulence LFO per voice.

#### Kolmogorov turbulence

$$
E(k) = C\, \varepsilon^{2/3}\, k^{-5/3} \quad\Longrightarrow\quad \lvert H(f) \rvert^2 \propto f^{-5/3} \ \text{above the corner } f_c
$$

**Code:** `KolmogorovNoise` (`KolmogorovNoise.h/.cpp`). It filters white Gaussian noise through a "−5/3 fractional pole" filter:

- **Inertial range:** three first-order pole/zero sections, with poles at 1, √10 and 10 × $f_c$. Each zero sits at $(\sqrt{10})^{5/6} \approx 2.61$ × its pole, so the magnitude falls on average at 5/6 of 20 dB per decade.
- **Dissipation range:** a final plain pole at 12 $f_c$.
- **Design:** the filter is designed once with the bilinear transform and prewarping, at 128 internal steps per hertz of corner frequency. It is normalised to unit output variance.

To move the corner, the code time-warps the internal clock instead of redesigning the filter: each call advances the phase by $128\, f_c\, \Delta t$, and the output is interpolated from the last four internal samples with 4-point Hermite interpolation. When the wind speeds up, the whole spectrum slides up in frequency with its shape and variance unchanged.

The tests require a fitted slope within ±0.25 of −5/3 over $[1.5, 8]\, f_c$ (measured: −1.76), unit variance (measured: 1.00), and a flat spectrum below the corner.

#### Storm gusts (von Kármán)

$$
S_u(f) \propto \left[1 + 70.8 \left(\frac{f L_u}{\bar U}\right)^2\right]^{-5/6}, \qquad
f_\mathrm{corner} = \frac{\bar U}{\sqrt{70.8}\, L_u} = \frac{\bar U}{8.41\, L_u}, \qquad
U(t) = \bar U \bigl(1 + I\, G(t)\bigr)
$$

The storm's large-scale wind is a mean speed $\bar U$ plus von Kármán turbulence $G(t)$, which has unit variance. By Taylor's frozen-turbulence hypothesis, stronger wind or a shorter integral length scale $L_u$ gives faster gusts.

- **Code:** `atmos::vonKarmanCornerHz()` and `StormWind::tick()`. The code uses $I = 0.6 \times$ `turbulence`. The mean glides with a 0.35 s time constant, and the gust factor $1 + I\,G$ is clamped to be at least 0. The corner uses $\max(\bar U, 1\ \mathrm{m/s})$, so even calm air still breathes.
- **Sound:** at the defaults (8 m/s, $L_u$ = 12 m) the corner is 0.079 Hz, which means gusts on roughly a 10-second scale. $U(t)$ and the gust factor drive everything else in the storm: rain rate, wire pitch and every voice's LFO.

#### Strouhal-linked note LFO

$$
f = \frac{St\, U}{L}, \qquad St = 0.2, \qquad L = \frac{c}{f_0},\ c = 343\ \mathrm{m/s}
\quad\Longrightarrow\quad f_\mathrm{LFO}(t) = \frac{St\, U(t)\, f_0}{c}
$$

Each note is treated as an obstacle as large as its own acoustic wavelength. Low keys are big obstacles that shed vortices slowly, and high keys are small ones that shed quickly. Because $U(t)$ is the instantaneous, gusting speed, the LFO itself speeds up in a gust.

- **Code:** `atmos::strouhalFrequency()` and `atmos::noteObstacleLength()`, used in `PianoVoice::controlTick()`. That function drives the voice's own `KolmogorovNoise` with its corner at $f_\mathrm{LFO}$, clamped to 0.02 to 16 Hz.
- **Sound:** at $U$ = 10 m/s the corner is 0.16 Hz at A0 ($L$ = 12.5 m), 1.5 Hz at C4, 12 Hz at C7, and 24 Hz at C8, which the clamp limits to 16 Hz. In the tests, the C7 LFO crosses zero more than 5× as often as the A1 LFO (measured: 764 vs 24 crossings in 8 s).

#### Fuse: notes bend like wind pitch

$$
U_\mathrm{key}(t) = \bar U\, g(t)\,\bigl(1 + I\, \ell(t)\bigr), \qquad
\frac{f'}{f} = \Bigl(\frac{U_\mathrm{key}}{\bar U}\Bigr)^{\delta}, \qquad
\delta = 0.06\cdot \mathrm{pitch}\cdot \varphi_w \cdot \min\!\bigl(\tfrac{\bar U}{8\ \mathrm{m/s}}, 2\bigr)
$$

An Aeolian tone's pitch is proportional to the wind speed ($f = St\,U/L$). Fused, each note follows the local wind at its key the same way, with the exponent $\delta$ taming the excursion: $\ell(t)$ is the key's Strouhal-rate LFO, $g$ the storm gust factor, pitch is `wind_pitch`, $\varphi_w = \sin(b\pi/2)$ of `wind_blend`, and stronger mean wind bends further. At the defaults this is roughly ±10 cents; at full settings in a gale it reaches the ±3 % (about ±51 cents) clamp.

- **Code:** `PianoVoice::controlTick()`. `PianoVoice::applyPitchRatio()` clamps the ratio to ±3 % and applies it by rotating every mode's pole by $\omega(\mathrm{ratio} - 1)$, so the whole stiff-string spectrum shifts while every decay rate stays the same. The tests require overlay to leave the pitch exactly alone and fuse to bend it by more than 3 cents rms.
- **Beyond the spec:** loud notes also start slightly sharp and settle, a tension-modulation glide of $1 + 0.0011\, v^2 e^{-t/70\,\mathrm{ms}}$ (up to +1.9 cents).

#### Fuse: wind timbre

$$
w_n = \frac{1 + E \big/ \sqrt{1 + Q^2 (r_n - 1/r_n)^2}}{1 + 0.3E}\cdot \Bigl(\frac{f_n}{f_1}\Bigr)^{\tau}, \qquad r_n = \frac{f_n}{f_\mathrm{bp}}, \qquad
f_\mathrm{bp} = 4 f_1 \cdot 2^{\,1.2\, s\, (0.55\,\ell + 0.45\, G)}, \qquad \tau = 1.5\cdot \mathrm{timbre}\cdot\varphi_w \ln\frac{U_\mathrm{key}}{\bar U}
$$

Gusts brighten the note (positive tilt $\tau$), lulls darken it, and a gentle resonance sweeps across the partials and swells with the gusts, blending the key's own LFO $\ell$ with the storm gust $G$.

- **Code:** `PianoVoice::controlTick()` and `PianoVoice::updateWeights()`. Q = 1.6; strength $s = \varphi_w \min(I U/4, 1.5)$; emphasis $E = 1.4 \cdot$ `wind_timbre` $\cdot \min(s, 1) \cdot (0.6 + 0.4 g)$; tilt clamped to ±0.6; amplitude swell $1 + 0.15 \cdot$ `wind_timbre` $\cdot \min(s, 1) \cdot \mathrm{clamp}(0.55\ell + 0.45G, \pm 1.5)$.
- **Approximation:** the band-pass is not a time-domain filter but a set of per-mode output weights in the modal sum: no latency and no filter state. A re-struck string keeps its current weighting, so re-strikes do not click.

#### Aeolian wires and wind roar (beyond the spec)

$$
f_\mathrm{wire} = \frac{St\, U(t)}{D_\mathrm{wire}}, \qquad D_\mathrm{wire} \in \{2.5,\ 4.0,\ 6.5\}\ \mathrm{mm}
$$

This is the **overlay** wind, in `WindAir.h`. Its level is `wind_air` × $\cos(b\pi/2)$ of `wind_blend`.

- **Wires:** three wires whistle at 800, 500 and 308 Hz at 10 m/s, and their pitch glides with the gusts. Each whistle is noise through a band-pass with Q = 28, normalised to unit variance. Vortex shedding only locks into a clean tone above a few m/s, so the whistles fade in linearly between 3 and 10 m/s.
- **Roar:** decorrelated low-passed noise at $90 + 22U$ Hz (left) and $95 + 23U$ Hz (right). Its gain follows the dynamic pressure, $\propto \min(U/20, 1.6)^2$.

### Thunder

#### MIDI velocity as distance

$$
x = x_\mathrm{max}\, \frac{127 - V}{126}, \qquad d = \frac{127 - V}{126} \in [0, 1]
$$

$V$ = 127 is overhead (0 m) and $V$ = 1 is at `storm_distance` ($x_\mathrm{max}$, 600 m by default). **Code:** `atmos::velocityToDistance()`, called from `PianoVoice::strike()`.

#### The strike: crack and re-contacts in the hammer-string contact

$$
F(t) = \Bigl[\underbrace{F_\mathrm{felt}(t)}_{\text{gamma pulse}} + \underbrace{A\, e^{-t/\tau}\, n(t)}_{\text{crack}}\Bigr] * h(t), \qquad
h(t) = \sum_{k=0}^{K-1} g_k\, \mathrm{LP}_k\, \delta(t - t_k)
$$

Thunder is not a separate sound. The lightning impulse is the hammer, and everything the spec describes for the strike happens in the force the string receives (`PianoVoice::buildForce()`):

- **Crack:** $n(t)$ is unit-variance white noise and $\tau$ runs from 10 ms at A0 to 4 ms at C8. $A = 0.2\,(1 - 0.9c) \cdot$ `crack_level` $\cdot\, v^{2.5} / \sqrt{\tau f_s/2}$, so harder strikes crack disproportionately more and the crack stays well under the felt pulse's low-frequency content. It is heard only through the string: the tests check that 99 % of a crack-only strike's energy lies on the string's partials.
- **Re-contacts (multipath):** the felt pulse returns from the near termination after one round trip, $t_\mathrm{rt} = \beta / f_1$, and re-contacts the hammer. They belong to the strike, so they follow `crack_level`: with depth $\mu = d \cdot 0.67 \cdot$ `crack_level`, $K = 1 + \mathrm{round}(4\mu)$ contacts are spaced by $(t_\mathrm{rt} + 2\ \mathrm{ms}\cdot\mu)\times U(0.6, 1.4)$ (the time-varying delay distribution, re-drawn every strike, capped at 20 ms), with gains falling by $0.4 + 0.25\mu$ per contact, normalised to unit sum, and each later contact darker (one-pole at $6 f_h/(1 + 0.9k)$). Later contacts fade fast, so a distant strike is a soft roll rather than a flam: the tests measure a 1.5 ms contact at $V$ = 127 and a 15.6 ms rolling contact at $V$ = 25 (6.4 ms without multipath).
- **Crack drive:** the crack is kept in a force signal of its own, which drives each mode by its shape on the string (the strike-position comb and radiation of the synthetic spectrum), normalised against the mezzo-forte felt pulse as before. The felt pulse itself drives the modes to their measured levels (see Piano I below).
- **Heard directly:** for I, the strike's measured attack residual (hammer, action and soundboard); for II, a synthetic soundboard knock, a decaying sinusoid at 70 to 160 Hz with τ from 35 to 17.5 ms and amplitude $0.3\,(1 - 0.6c)\,c \cdot$ `crack_level` $\cdot\, v$. Both start with the hammer's 0 to 1 ms jitter.

#### Atmospheric absorption

$$
P(f, x) = P_0(f)\, e^{-\alpha(f)\, x}, \qquad \alpha(f) = \alpha_{1k} \left(\frac{f}{1\ \mathrm{kHz}}\right)^2, \qquad \alpha_{1k} = 4.6\times10^{-4}\ \mathrm{Np/m} \approx 4\ \mathrm{dB/km}
$$

Absorption is applied **exactly, partial by partial**: every mode's drive is weighted by $e^{-\alpha(f_n)\, x\, a}$, where $a$ is `air_absorption`. Because the whole strike (felt pulse, crack and multipath) drives the modes through those weights, a distant strike reaches the string darker, so the cutoff falls as distance grows.

- **Code:** `atmos::absorptionGain()` in `PianoVoice::strike()`.
- **Sound:** in the tests ($x_\mathrm{max}$ = 1500 m, `air_absorption` 0.4, `crack_level` 1), the spectral centroid of the first 43 ms of C4 falls from about 2.8 kHz at $V$ = 127 to 0.47 kHz at $V$ = 80 and 0.44 kHz at $V$ = 30.
- **The residual:** Piano I's measured attack noise is darkened the same way in the time domain, by two one-poles at `atmos::absorptionCascadeCutoff()`, the cutoff of an $N$-pole cascade matched to the Gaussian, $f_c = 1\ \mathrm{kHz}\sqrt{N/(2\alpha_{1k}x)}$ with $N$ = 2.

#### The roll in the low strings

$$
w(k) = \mathrm{clamp}\!\left(\frac{60 - k}{39}, 0, 1\right)^{1.5}, \qquad
D = \min\bigl(1,\ 1.6\, w(k)\cdot \mathrm{rumble}\cdot(0.6 + 0.4d)\bigr), \qquad
r(t) = \sum_k a_k \frac{t - t_k}{\tau_k} e^{1 - (t - t_k)/\tau_k}
$$

$$
m(t) = \mathrm{clamp}\bigl(D \min(r(t), 1.2),\ 0,\ 1\bigr), \qquad
f_c = f_1\, 2^{\,6 - 4.5\,m}, \qquad
\lvert H(f)\rvert = \bigl(1 + (f/f_c)^4\bigr)^{-1/2}, \qquad
\frac{f'}{f} = 2^{-8m/1200}
$$

After the strike, the thunder rolls through the string (`PianoVoice::controlTick()`), only below middle C and more the lower the key: $w(k)$ is 0 from C4 up and 1 at A0, where the note's partials sit in thunder's own register (rumble energy peaks around 50 to 150 Hz). The roll is 2 to 4 swells: the first, the clap, 30 to 100 ms after the strike; the rest spread over $T = $ `rumble_decay` $\cdot\,(0.5 + 0.5d)$, with widths $\tau_k = (0.12 + 0.25\,U)(0.7 + 0.3d)$ s and heights $a_0 = 1$, $a_k = (0.55 + 0.45\,U)\,e^{-0.5\,t_k/T}$ ($U$ uniform, from the voice's seeded RNG). Distant (soft) strikes roll longer and deeper, which is how the spec's "decay and wet mix grow as velocity falls" now shows up.

Each swell is colour, not volume:

- **Darker:** a two-pole low-pass on the modal weights, from 6 octaves above the fundamental (open) down toward $2.8 f_1$ at full depth: about 200 Hz for a soft A0 at the defaults, thunder's register.
- **Loudness held:** a makeup gain $(E_\mathrm{open}/E_\mathrm{filtered})^{0.45}$ (clamped to 1–4), computed on what the string holds at that moment, restores 90 % of the level the filter takes; a $1 + 0.1m$ swell is the only volume left.
- **Lower:** the pitch sags by up to 8 cents.
- **Body:** under the roll the body rumbles, noise through a state-variable low-pass (Q 0.9) at $55 + 35 k_n$ Hz at $0.08\,m\sqrt{E_\mathrm{strike}}$.

In the tests (V = 50, `rumble_mix` 0.5), the deepest darkening of the spectral centroid is 0 % at C3, 25 % at E1 and 49 % at A0, the level stays within +0.5 dB, nothing swells, and a soft A0 sags 5.6 cents with its low-pass reaching 194 Hz. From middle C up, the roll is absent: a close C5 renders bit-identically with or without it.

The rumble network of earlier versions (three rolling-tap and FDN zones fed by every voice) is gone: it swelled after soft notes.

### The piano (beyond the spec)

#### Piano I: a real grand, resynthesised

Piano I is the Salamander Grand Piano V3 (a Yamaha C5 recorded by Alexander Holm, CC-BY 3.0): every third key (A0, C1, D♯1 … C8) at 16 velocity layers, 480 recordings. They are not played back. `Tools/hybrid/analyse_salamander.py` analyses each one into this engine's own modal model, and `PianoHybrid` rebuilds any key and velocity from the result. This is analysis and resynthesis (Serra and Smith's spectral modelling: deterministic partials plus a stochastic residual), with the soundboard folded into both, as in commuted synthesis:

$$
\lvert X_n(t)\rvert = \bigl\lvert\, a_1 e^{-s_1 t} + a_2 e^{-s_2 t} e^{\,i(2\pi \beta t + \phi)} \bigr\rvert
$$

- **Partials:** each partial $n$ is tracked (left and right analysed separately and their powers averaged, so the spaced microphones never comb-filter each other) and fitted, in the log domain over the frames well above the floor between partials, with the exact pair of complex resonators each partial has in `PianoVoice`: a prompt mode ($a_1$, $s_1$) and an aftersound mode ($a_2$, $s_2$) offset by the beat $\beta$ at phase $\phi$. 18,304 fits; median error 1.4 dB, 90th percentile 2.9 dB.
- **What is stored:** per sampled key and partial (up to 96), the measured frequency ratio $f_n/f_1$, the decay rates, the aftersound level, beat and phase (string properties, pooled over the louder layers), and the prompt amplitude per velocity layer (smoothed across layers). Partials lost in the floor continue the spectrum's slope; extrapolation back to the strike is capped at +12 dB; the loader clamps everything to physical ranges.
- **The residual:** each partial is heterodyned to DC, low-passed (4-pole Butterworth, bandwidth $\mathrm{clamp}(0.35 f_1, 6, 80)$ Hz), re-modulated and subtracted. What is left of the first 250 ms (hammer, action and soundboard noise: about −24 dB under C4's partials, and most of the sound at C8) is stored at 4 velocity layers, μ-law coded.
- **In the voice:** `PianoHybrid::partials()` interpolates the sampled keys (amplitudes in dB, decays in log, ratios linearly) and the velocity layers (by the SFZ's layer velocities; below the softest layer, amplitude ∝ velocity). The data is normalised to unit energy at $V$ = 80 per key and set level with the synthetic spectra by the mezzo-forte impulse 0.63. Each mode's drive is its measured amplitude divided by the spectrum the felt pulse delivers there, $\mathrm{drive} = a\, e^{-\alpha x}/(v\,\lvert F(f)\rvert)$, so the modes ring at the recorded levels while the storm still acts on them; the aftersound's measured phase is an imaginary drive. `hammer_hardness` and the una corda re-voice the recording by the ratio of the felt spectra at the actual and the default hardness. `unison` scales the measured beats (1.2 ct = as recorded); `sustain` divides the measured decay rates.
- **The attack:** the residual of the nearest sampled key is re-pitched to the key played ($f_1/f_{1,\mathrm{sample}}$, also converting the sample rate), crossfaded at equal power between the two nearest velocity layers, darkened by distance, and added to the voice.
- **Regenerating:** `python3 Tools/hybrid/analyse_salamander.py <Salamander>/Samples Source/DSP/Data/piano_hybrid.bin` (numpy, scipy and ffmpeg; a few minutes on 4 cores). The 1.5 MB blob is embedded at build time by `Tools/BinToCpp.cpp`, so the engine stays plain C++.

Against the recordings (C2, C4, A4, weather off), the resynthesised Piano I matches the attack's ⅓-octave spectrum to 2.8–4.8 dB (the old analytic piano: 7–16 dB) and the first 12 partials' envelopes to 3.6–8 dB (7–10 dB). The tests check C4's partials 2 and 3 against the recording to 2.5 dB (measured: 0.2 and 0.6 dB off) and its double decay: 20 dB/s, then 1.8 dB/s.

#### I ↔ II: grand to tine

`piano_character` $c$ morphs every partial of the voice (`PianoVoice::strike()`), captured when the key is struck:

- **Frequency:** $f_n = f_n^{\mathrm{I}}\,(f_n^{\mathrm{II}}/f_n^{\mathrm{I}})^{c}$, from the measured, stretched partials of the grand (I) to exact harmonics $n f_1$ of a tine heard through its pickup (II). Stretch tuning fades out with $c$.
- **Spectrum:** amplitudes blend linearly, $a_n = (1-c)\,a^{\mathrm{I}}_n + c\,a^{\mathrm{II}}_n$ (the aftersound as a complex amplitude), both at the same mezzo-forte reference so loudness holds through the morph. II's pickup spectrum is $\hat g^{\mathrm{II}}_n \propto b^{\,n-1}/\sqrt n \,\cdot\, (1 + (n f_1/2\,\mathrm{kHz})^2)^{-1/2}$ with bark $b = (0.16 + 0.5\,v^{1.5} + 0.12\,(1 - k_n))(1 - 0.45 k_n)$ (up to 0.8): near-sine when played softly, a growl of 2nd and 3rd harmonics when struck hard, thinning toward the treble so high notes stay round. Partial 7's prompt mode becomes the tine's own overtone, a short bell at $6.9 f_1$ ($T_{60}$ 0.35 s) at level $(0.10 + 0.22v)(1 - 0.75k_n)/\sqrt{1 + (6.9 f_1/3.5\,\mathrm{kHz})^2}$, so it fades out in the treble.
- **Decay:** $\sigma_n$ moves geometrically from the measured rates to the tine's $\sigma_1^{\mathrm{II}}(1 + 0.9(n-1))$, $T_{60}^{\mathrm{II}} = 10\,\mathrm{s}\cdot 0.25^{k_n}$: the bark fades into a long, smooth near-sine. The aftersound mode becomes a whisper of chorus (0.35 cents, level 0.18).
- **Hammer:** the felt corner (580 Hz at C4) moves to a soft neoprene tip (1.8 kHz at C4, rising only 0.15 octave per octave, order 2.4, weaker velocity dependence); the crack falls to 10 % and the soundboard knock to 40 % at II.
- **Onset:** II's output swells in through a one-pole gain with time constant $c\,(0.8 + 3.5 k_n)$ ms, so a tine never clicks on; re-striking a sounding key keeps the gain where it is. Measured at $V$ = 100: a 10 to 13 ms 10–90 % rise from C5 to G♯7.
- **Stereo:** prompt and aftersound modes are summed into separate lanes; their difference is a "side" signal (width 0.22 at I, 0.55 at II, times `stereo_width`) that wanders as the two beat, like a spaced microphone pair.
- **Level:** a II-specific trim (+9 dB tapering to 0 at mid-keyboard, falling to −10 dB at C8) keeps the tine even *to the ear*: II's treble is a near-sine at 2 to 4 kHz, where the ear is most sensitive, so it is trimmed by roughly the ISO 226 equal-loudness difference rather than by RMS (`--calibrate 1` reads about −32 dB at C4 and −38 dB at C8).

#### Sympathetic resonance (I)

After the "virtual resonance modelling" of digital pianos: undamped strings ring along with whatever is played. `SympatheticResonance` is a bank of 24 tuned feedback combs (A1 to G♯3, whose harmonics cover everything above), each with a one-pole loss filter at 2.6 kHz, fed by the dry piano bus. With the pedal down the strings ring for about 3.5 s; with it up, a faint 0.5 s bloom remains. Level `resonance` × $(1-c)$: a tine piano has no free strings.


The spec treats the hammer strike as an ideal impulse $\delta(t)$ that triggers the weather. Petrichor renders an actual piano for that impulse to strike. The analytic string below is the fallback for Piano I (used only if the measured data cannot be loaded) and the base the tine morphs from.

#### Stiff string

$$
f_n = n\, f_0 \sqrt{1 + B n^2}, \qquad f_0 = \frac{f_1}{\sqrt{1 + B}}, \qquad f_1 = A_4 \cdot 2^{(k - 69)/12} \cdot 2^{s(k)/1200}
$$

$f_0$ is chosen so that partial 1 lands exactly on the tuned fundamental $f_1$.

- **Inharmonicity:** $B(k)$ (`PianoVoice::inharmonicity()`) is log-linear across the keyboard: 3×10⁻⁴ at A0, a minimum of 10⁻⁴ at A2, and 8×10⁻³ at C8.
- **Stretch tuning:** the Railsback stretch (`PianoVoice::stretchCents()`) is $s = 2.6\times10^{-4}\, d^3$ cents for $d = k - 69 < 0$ and $4.6\times10^{-4}\, d^3$ cents otherwise. That is −28.8 cents at A0 and +27.3 cents at C8.
- **Partial limit:** partials stop at $\min(0.45 f_s, 14\ \mathrm{kHz})$, with at most 96 partials.

The tests check A4 at 440 ± 0.5 Hz and the 12th partial of C2 against the measured ratio to within 0.6 Hz (Piano I's partials sit where the recorded C5's did: 785.4 Hz, against 779.8 Hz for an exact harmonic).

#### Loss and double decay

$$
\sigma_n = \sigma_1 + b_3 f_n^2, \qquad \sigma_1 = \frac{6.91}{T_{60}(k) \cdot \mathrm{sustain}}, \qquad b_3 = \frac{2.6\times10^{-7}}{\mathrm{sustain}}, \qquad T_{60}(k) = 14\ \mathrm{s} \cdot 0.045^{(k-21)/87}
$$

The fundamental's prompt T60 is 14 s at A0, 2.5 s at A4 and 0.63 s at C8 (`PianoVoice::promptT60()`).

Each partial is rendered by two complex one-pole resonators, $z = e^{-\sigma/f_s} e^{j\omega}$:

- **Prompt mode:** decay rate $\sigma_n$, tuned $+c/2$ cents.
- **Aftersound mode:** decay rate $0.22\,\sigma_n$, tuned $-c/2$ cents, at level 0.32, or 0.5 under the una corda pedal.

Together they reproduce the beating and the two-stage decay of real unison strings. The detune $c$ is `unison`, halved for A0 to G1, scaled per key by 0.6 to 1.4, and scaled per partial by 0.75 to 1.25.

#### Excitation

For the synthetic spectra (the tine, and I's fallback), each partial's shape is the product of three factors. The same shape says how the crack, a raindrop or the rain hiss reaches each mode, for Piano I too:

- **Strike-position comb:** $0.03 + 0.97\,\lvert\sin(n\pi\beta)\rvert$, with $\beta = 0.122 - 0.05\,k_n^2$, about 1/8 of the string length.
- **Radiation:** a high-pass at 90 Hz and a gentle roll-off above 7 kHz.
- **Soundboard:** a factor of 0.8 to 1.2.

The random factors (soundboard, unison) come from a stateless hash of the key and partial number, so every instance of the plug-in is the same piano. Drive is normalised against the mezzo-forte hammer spectrum of the key, so loudness follows velocity, not key brightness.

#### Gamma-pulse hammer

$$
F(t) \propto t^{\kappa - 1} e^{-t/\theta} \quad\Longrightarrow\quad \lvert F(f) \rvert \propto \left[1 + (f/f_h)^2\right]^{-\kappa/2}, \qquad f_h = \frac{1}{2\pi\theta}
$$

The felt hammer is modelled as a smooth, causal force pulse whose spectrum has no nulls. With $v = V/127$, `PianoVoice::strike()` computes:

- **Hammer speed:** $u_h = 0.35 \cdot 17^{v}$, from 0.35 to 6 m/s.
- **Spectral corner:** $f_h = 650\ \mathrm{Hz} \cdot 2^{0.6(k-60)/12} \cdot 2^{1.6(h - 0.5)} \cdot (u_h / 2.08)^{0.6}$, where $h$ is `hammer_hardness`, reduced by 0.25 under the soft pedal.
- **Order:** $\kappa = 2.6 - v$.
- **Pulse area:** $v$, scaled by 0.75 under the soft pedal.

A harder strike compresses the felt for less time, so it sounds brighter. The pulse feeds every mode directly (`tickModesDriven()`).

**Hammer jitter:** each strike starts after a random delay of 0 to 1 ms. This stops the notes of a MIDI chord from landing on the same sample and summing into one coherent spike.

#### Dampers, stealing and level

- **Dampers:** felt dampers have a T60 of $0.6\ \mathrm{s} \cdot 0.2^{(k-21)/68}$, from 0.6 s at A0 to 0.12 s at F6. Keys from F♯6 (MIDI 90) up have no dampers.
- **Voice stealing:** when all 64 voices are busy, the engine steals the voice with the least kinetic energy, $\sum \lvert y \rvert^2$, over all modes. Released and pedal-sustained voices go before held ones. The stolen voice fades out over 4 ms, and the new note starts on the first control tick after the fade. Striking a key that is still sounding adds to the string's existing motion rather than restarting it.
- **Loudness calibration:** Piano I's output gain is $0.1$ times a per-key trim (`kMeasuredTrimDb`, one value per sampled key, interpolated), measured with `PetrichorRender --calibrate 0 1`: every key from A0 to C8 at V = 80 reads −32.1 dB ± 0.8 dB (a few keys in the top octave, where the attack noise is the nearest sample's, up to 3 dB off). The tine (II) uses $10^{9\max(0,\, k_n - 0.35)/20}$ plus its own trim.
- **Master stage:** a soft knee that is linear up to 0.8 (−1.9 dBFS) and compressed above with $0.8 + 0.2\tanh\!\bigl((\lvert x\rvert - 0.8)/0.2\bigr)$. The output never exceeds 0 dBFS.

### Spec vs. implementation at a glance

| Spec | Implementation |
|------|----------------|
| Hammer = lightning impulse $\delta(t)$ | The hammer *is* the lightning: a gamma felt pulse plus the crack $A e^{-t/\tau} n(t)$, convolved with soft re-contacts, drives the string; only the strike's own noise (measured for I) is heard directly |
| Transient through a distance-dependent low-pass | Exact $e^{-\alpha(f_n) x}$ weighting of every partial's excitation |
| Multi-tap delay or convolution $h(t)$; decay and wet mix vs. velocity | In the contact: up to 5 soft re-contacts for distant strikes. In the low strings: a rolling low-pass, pitch sag and body rumble, longer and deeper for distant strikes |
| fBm spatial gust field | Temporal −5/3 noise (fBm with H = 1/3), with independent realisations per voice and per tap instead of a spatial field |
| Kolmogorov −5/3 LFO | Pole/zero cascade, time-warped so the Strouhal corner moves without changing the spectrum's shape |
| Strouhal LFO from the note | $L = c/f_0$ (the note's wavelength); $U$ is the instantaneous gusting speed |
| Micro-pitch vibrato | Fuse: the note's pitch follows the local wind like an Aeolian tone, $(U_\mathrm{key}/\bar U)^\delta$; overlay leaves it alone |
| Resonant band-pass sweep | Per-mode weights in the modal sum, plus a gust brightness tilt and swell |
| Granular rain layer over the sustain | Overlay: grains + hiss that follow the wind and the piano. Fuse: drops on strings, hiss in the modes, patter modulation |
| Rain density from $N(D)$ | Drop flux $\int N v_T\, dD$ × 0.015 m² as a Poisson rate; flux-weighted diameters |
| Rain linked to wind LFO amplitude | $R = R_0 (1 + \min(\bar U/8, 1)(g - 1))^{3c}$, from the storm-wide gust factor |
| Brighter rain in gusts | Each drop's tick brightens with the vector impact speed ($1200\,\lvert\mathbf v\rvert^{0.7}$ Hz); the distant-rain bed's bandwidth follows the mean impact speed |
| (not in spec) | Piano I resynthesised from a real grand, a Rhodes-style tine (II), Minnaert bubbles, Aeolian wire whistles, wind roar, tension glide, energy-based voice stealing |

---

## MIDI mapping

All MIDI channels are treated alike. MIDI is handled in `PluginProcessor::handleMidi()` and forwarded to `PetrichorEngine`.

| Message | Engine call | Effect |
|---------|-------------|--------|
| Note-on **key** | `noteOn()` → `PianoVoice::strike()` | Pitch (A4 tuning plus Railsback stretch), $B$, T60, strike point, hammer corner, pan (bass left). Sets the Strouhal obstacle $L = c/f_0$ and therefore the wind LFO tempo. |
| Note-on **velocity** $V$ | `noteOn()` | Hammer speed $0.35 \cdot 17^{V/127}$ m/s (brightness and contact time) **and** distance $x = x_\mathrm{max}(127 - V)/126$, which sets the tone absorption, the hammer's re-contacts and how long and deep the thunder rolls in the low strings. |
| Note-off, or note-on with velocity 0 | `noteOff()` | Lowers the damper, unless the sustain pedal is down or the key is F♯6 (MIDI 90) or higher, which has no damper. |
| Repeated note-on of a sounding key | `noteOn()` | Re-strikes the same voice, adding to its motion. |
| **CC64** sustain (≥ 64 = down) | `setSustainPedal()` | Dampers stay up; on release, every voice whose key is no longer held is damped. |
| **CC67** soft pedal (≥ 64 = down) | `setSoftPedal()` | Una corda, for notes struck while it is down: hardness −0.25, impulse ×0.75, and (I) 1.5 × the measured aftersound and 0.8 × the attack noise. |
| **CC120** All Sound Off | `allSoundOff()` | Instant silence: silences every voice, clears the sympathetic strings and releases both pedals. |
| **CC123** All Notes Off | `allNotesOff()` | Releases both pedals, drops queued notes and damps every voice. Undamped treble strings still ring out. |
| **CC121** Reset All Controllers | `setSustainPedal(false)`, `setSoftPedal(false)` | Releases the pedals. Keys still held keep sounding. |
| Pitch bend, mod wheel, aftertouch, other CCs | none | Ignored. |

---

## Parameters

All 27 parameters are defined in `Source/Plugin/Parameters.h`, which is the single source of truth. Each one is an automatable `AudioParameterFloat` with `ParameterID{id, 1}`, mapped by member pointer onto `EngineParams` in physical units. Where the "Skew centre" column has a value, that value sits at the knob's midpoint (`setSkewForCentre`). Unitless 0 to 1 controls are displayed as percentages.

| id | Name | Range | Default | Skew centre | What it does physically |
|----|------|-------|---------|-------------|-------------------------|
| `hammer_hardness` | Hammer Hardness | 0 – 1 | 0.5 | | Felt stiffness. Scales the hammer's spectral corner by $2^{1.6(h-0.5)}$ (±0.8 octave). |
| `sustain` | Sustain | 0.3 – 3 × | 1.0 | 1.0 | Multiplies every string T60 (divides the measured decay rates; for II, $\sigma_1$ and $b_3$). |
| `unison` | String Detune | 0 – 4 ct | 1.2 | | Unison beating: scales the measured beats of Piano I (1.2 = as recorded); II's chorus detune. |
| `stereo_width` | Stereo Width | 0 – 1 | 0.7 | | Keyboard pan spread, bass left and treble right, up to ±0.85. |
| `piano_character` | Piano I/II | 0 – 1 | 0 | | I (a real grand, resynthesised from the Salamander C5) ↔ II (Rhodes-style tine); morphs every partial. |
| `resonance` | String Resonance | 0 – 1 | 0.5 | | I: sympathetic string resonance, blooming with the sustain pedal. |
| `piano_level` | Piano Level | −24 – +6 dB | 0 | | Gain of the string bus. |
| `tuning` | Tuning A4 | 415 – 466 Hz | 440 | | Reference pitch, before stretch tuning. |
| `storm_distance` | Storm Distance | 50 – 4000 m | 600 | 800 | $x_\mathrm{max}$: the distance of a velocity-1 strike. Velocity 127 is always overhead. |
| `crack_level` | Crack | 0 – 1 | 0.3 | | The strike's edge: amplitude $A$ of the crack in the hammer's force (heard through the string), the hammer's re-contacts ($\mu = 0.67\,d \cdot$ value) and II's soundboard knock. |
| `air_absorption` | Air Absorption | 0 – 1 | 0.35 | | Fraction $a$ of the strike distance applied to every partial's excitation as $e^{-\alpha(f_n)xa}$. |
| `rumble_mix` | Rumble | 0 – 1 | 0.5 | | Depth of the thunder's roll through the low strings, $D = 1.6\,w(k) \cdot$ value $\cdot\,(0.6 + 0.4d)$: below middle C, a rolling low-pass toward thunder's register, a pitch sag and a body rumble. Colour, not volume. |
| `rumble_decay` | Rumble Decay | 0.5 – 12 s | 3.5 | 4.0 | How long the roll lasts, $T = $ value $\cdot\,(0.5 + 0.5d)$. The plug-in reports a tail of 30 s plus this value. |
| `wind_speed` | Wind Speed | 0 – 30 m/s | 8.0 | 8.0 | Mean wind $\bar U$, which glides with a 0.35 s time constant. |
| `turbulence` | Turbulence | 0 – 1 | 0.35 | | Turbulence intensity $I = \sigma_u/\bar U = 0.6 \times$ value. |
| `gust_length` | Gust Length | 2 – 100 m | 12 | 15 | Integral length scale $L_u$. The gust corner is $\bar U/(8.41 L_u)$, so longer means slower gusts. |
| `wind_blend` | Wind Overlay/Fuse | 0 – 1 | 0.7 | | Equal-power blend: overlay (audible wind layer) ↔ fuse (wind inside the notes). |
| `wind_pitch` | Wind Pitch | 0 – 1 | 0.35 | | Fuse: depth of the Aeolian pitch law, $\delta = 0.06 \cdot$ value $\cdot \varphi_w \cdot \min(\bar U/8, 2)$. |
| `wind_timbre` | Wind Timbre | 0 – 1 | 0.4 | | Fuse: gust brightness tilt, band-pass emphasis and the ±15 % swell. |
| `wind_air` | Air Level | 0 – 1 | 0.3 | | Overlay: level of the audible wind, roar $\propto U^2$ and Aeolian wire whistles. |
| `rain_rate` | Rain Rate | 0 – 150 mm/h | 8.0 | 15 | $R_0$, the rainfall rate at the mean wind speed. |
| `rain_coupling` | Wind Coupling | 0 – 1 | 0.6 | | $c$ in $R = R_0(1 + \min(\bar U/8,1)(g-1))^{3c}$: how strongly rain follows the gusts. |
| `rain_level` | Rain Level | 0 – 1 | 0.35 | | Texture amount $\ell$ in both modes: overlay gain $3.15\,\ell^{1.5}$, fused drop power, hiss and patter depth. At 0, no drops are generated. |
| `rain_surface` | Puddles | 0 – 1 | 0.3 | | Ground type, from 0 (leaves and soil) to 1 (standing water). Sets the probability and level of Minnaert bubbles (overlay). |
| `rain_blend` | Rain Overlay/Fuse | 0 – 1 | 0.5 | | Equal-power blend: overlay (rain layer beside the piano) ↔ fuse (rain inside the notes). |
| `rain_follow` | Piano Follow | 0 – 1 | 0.6 | | Overlay: how much the rain layer follows the piano's envelope and spectral centre. |
| `master` | Master | −24 – +6 dB | −2.0 | | Output gain ahead of the soft-knee limiter. |

---

## Building

### Prerequisites

| | Requirement |
|---|---|
| All platforms | CMake ≥ 3.22, a C++17 compiler, Git (to fetch JUCE 8.0.9). For the plug-in UI only: Node.js 18+ with npm. |
| Linux (Debian/Ubuntu) | `pkg-config` and the JUCE development packages listed below |
| Windows | Visual Studio 2022 (MSVC) and the **WebView2 SDK**, i.e. the NuGet package `Microsoft.Web.WebView2` (see below) |
| macOS | Xcode or the Xcode Command Line Tools. AU is built only on macOS. |
| Regenerating Piano I's data (optional) | Python 3 with numpy and scipy, ffmpeg, and the Salamander Grand Piano V3 samples. The generated blob is in the repository, so a normal build does not need any of this. |

To install the Linux packages:

```sh
sudo apt install build-essential cmake git pkg-config \
  libasound2-dev libjack-jackd2-dev libcurl4-openssl-dev libfreetype-dev libfontconfig1-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev \
  libxrender-dev libwebkit2gtk-4.1-dev libgtk-3-dev
```

The project sets `JUCE_USE_CURL=0`, so `libcurl4-openssl-dev` is optional. On older distributions, `libwebkit2gtk-4.0-dev` also works.

**Windows and WebView2:** Windows builds always use WebView2, because the editor serves its page through the web view's resource provider, which only the WebView2 backend supports. The build statically links the WebView2 loader (`NEEDS_WEBVIEW2` and `JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1`), so CMake must be able to find the NuGet package. JUCE's `FindWebView2.cmake` suggests installing it from PowerShell:

```powershell
Register-PackageSource -provider NuGet -name nugetRepository -location https://www.nuget.org/api/v2
Install-Package Microsoft.Web.WebView2 -Scope CurrentUser -RequiredVersion 1.0.1901.177 -Source nugetRepository
```

If the package is installed somewhere else, point JUCE at it with `-DJUCE_WEBVIEW2_PACKAGE_LOCATION=<dir>`.

End users need the Edge WebView2 Runtime, which ships with Windows 11 and current Windows 10. On every OS, the editor uses the system web view: WebView2 on Windows, WKWebView on macOS and WebKitGTK on Linux.

### Engine only: tests and offline renderer

This build needs no JUCE, no npm and no network.

```sh
cmake -S . -B build-engine -DPETRICHOR_ENGINE_ONLY=ON
cmake --build build-engine --parallel
ctest --test-dir build-engine --output-on-failure   # or: ./build-engine/PetrichorTests

mkdir -p renders
./build-engine/PetrichorRender renders               # writes 7 demo WAVs (24-bit, 48 kHz, stereo)
./build-engine/PetrichorRender --calibrate           # per-key loudness table
```

`PetrichorRender <dir>` writes into an existing directory (the default is `.`). Renders are deterministic, so a before/after comparison of the files is a valid regression check.

| File | What you hear |
|------|---------------|
| `01_velocity_is_distance.wav` | The same D-minor chord at V = 127, 100, 72, 44 and 18: from an overhead crack to a distant, darker strike rolling in the bass |
| `02_wind_overlay_to_fuse.wav` | Held chords in a gusty 14 m/s wind while `wind_blend` sweeps from overlay (the wind's roar and whistles, 0–8 s) to fuse (the notes themselves bend and brighten, from about 24 s) |
| `03_rain_overlay_to_fuse.wav` | A slow phrase while `rain_blend` sweeps from an overlay rain layer that follows the piano (0–8 s) to rain that only exists inside the notes (from about 24 s) |
| `04_storm_prelude.wav` | A short D-minor prelude as the storm builds (wind 4 → 16 m/s, rain 3 → 28 mm/h), with a fortissimo strike and distant rumbles in the coda |
| `05_dry_piano.wav` | The bare instrument: A0 to A7 at V = 90, then a C chord at V = 20 to 120 |
| `06_piano_I_to_II.wav` | The same phrase three times: the resynthesised grand (I), halfway, and the Rhodes-style tine (II), with a hard hit (bark) and a soft one (near-sine) |
| `07_thunder_roll.wav` | A0, E1 and an A1 chord three times: without the roll, at the default depth, and at full depth: darker and a few cents lower while it rolls, never louder |

`--calibrate [c] [step]` strikes every `step`-th key (default 3, the sampled keys; 1 for all) at piano character c (default 0) from A0 to C8 at V = 80, with all weather off. It prints the RMS level of the first 0.5 s, after a 100 Hz high-pass. Use it after any change that affects level, and keep the curve flat.

The test suite (`Tests/EngineTests.cpp`) has no framework dependency. It runs 66 checks in 17 tests:

- the Marshall-Palmer $\Lambda$ and the flux-weighted sampling,
- $v_T$ and the vector sum,
- the Strouhal mapping,
- the $f^2$ absorption law and the cascade fit,
- the Kolmogorov slope and variance,
- A4 pitch and C2's measured, stretched partials,
- the velocity → distance → brightness chain,
- the thunder's roll: only below middle C, growing toward A0, darker but not louder, never swelling, a few cents lower, C5 bit-identical with or without it,
- Piano I against the recording: partial levels, the double decay, the strike's noise between the partials,
- a treble LFO faster than the bass,
- gust-coupled rain density and brightness,
- thunder in the hammer-string contact (one contact when close, a longer rolling contact when distant; the crack heard mostly through the string),
- wind overlay leaving pitch alone and wind fuse bending it,
- overlay rain following the piano, fused rain silent without notes and texturing sustained ones,
- piano I ↔ II: II rounder than I, harmonic, barking harder when struck harder; sympathetic strings ringing with the pedal,
- stability with extreme settings at 96 kHz (finite output, peak ≤ 1),
- 64 sustained voices with all weather running faster than real time.

### Full plug-in

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

What this build does:

- **JUCE 8.0.9** is fetched with `FetchContent` at configure time, which needs network access. To use a local checkout instead, which is faster and works offline, add `-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE`. The checkout should be at tag 8.0.9.
- **The UI** is rebuilt by CMake whenever the frontend sources or configuration change (`frontend/src/**`, `index.html`, `package.json`, `package-lock.json`, or the Vite, Tailwind and PostCSS configs). The rebuild runs `npm ci` (including devDependencies) followed by `npm run build`. If npm is not installed, CMake uses an existing `frontend/dist/index.html`, and fails if there is none.
- **Formats** are VST3, AU (macOS only) and Standalone. The artefacts go to `build/PetrichorPiano_artefacts/Release/{VST3,AU,Standalone}/`. `COPY_PLUGIN_AFTER_BUILD` is off, so copy `Petrichor Piano.vst3` (or `.component`) into your plug-in folder yourself.
- **Tests and renderer** are also built in this mode (`PETRICHOR_BUILD_TESTS=ON`). If they get in the way, pass `-DPETRICHOR_BUILD_TESTS=OFF`.

| CMake option | Default | Effect |
|--------------|---------|--------|
| `PETRICHOR_ENGINE_ONLY` | `OFF` | Build only the engine, tests and renderer, with no JUCE and no npm |
| `PETRICHOR_BUILD_TESTS` | `ON` | Build `PetrichorTests` and `PetrichorRender` |
| `FETCHCONTENT_SOURCE_DIR_JUCE` | unset | Use a local JUCE checkout instead of fetching one |

The engine is always compiled with `-O3` on GCC and Clang, even in Debug, because the modal bank relies on auto-vectorisation.

---

## Frontend ↔ backend contract

The editor (`Source/Plugin/PluginEditor.*`) is a single `juce::WebBrowserComponent` that hosts the React UI. The UI's internals are free to change, but it must keep to this contract.

**Page loading.** On macOS and Windows the editor serves the bundled `index.html` from memory through JUCE's resource provider. On Linux, JUCE 8.0.9 relays resource-provider responses to its WebKit helper process through a pipe as JSON and drops messages that arrive in pieces; the ~1.4 MB encoded page does, which crashes the helper. So on Linux the editor writes the page once to `$TMPDIR/PetrichorPiano/ui-<hash>.html` and loads it from disk (`PluginEditor::pageUrl()`). Native integration (relays, native functions, events) works the same either way.

- **Delivery:** Vite and `vite-plugin-singlefile` build the UI into **one self-contained `frontend/dist/index.html`**, with scripts, styles and fonts all inlined. `juce_add_binary_data(PetrichorFrontend)` embeds that file in the binary. The resource provider serves only `/` and `/index.html`; any other URL returns not found, and nothing is ever loaded from the network.
- **Parameters:** each parameter has a `juce::WebSliderRelay` named exactly by its parameter id, bound to the APVTS parameter with a `WebSliderParameterAttachment`. The relay names are listed in `window.__JUCE__.initialisationData.__juce__sliders`. Each relay talks on the event `"__juce__slider" + id`, using JUCE 8's slider protocol (the same one behind `getSliderState()` in JUCE's `index.js`):
  - **UI → plug-in:** `{eventType: "requestInitialUpdate"}`, `{eventType: "valueChanged", value: <scaled>}`, `{eventType: "sliderDragStarted"}` and `{eventType: "sliderDragEnded"}`.
  - **Plug-in → UI:** `{eventType: "valueChanged", value: <scaled>}`, and `{eventType: "propertiesChanged", start, end, skew, name, label, numSteps, interval, parameterIndex}`.

  The `skew` carries the `Parameters.h` skew centre, so the UI maps knob position exactly as the host does. `frontend/src/params.js` mirrors the ids, ranges, defaults and skew centres in `Parameters.h`. If they disagree, `Parameters.h` wins.
- **Native functions** (listed in `__juce__functions`):
  - `noteOn(key, velocity)`: key 0 to 127, velocity 1 to 127, in MIDI units.
  - `noteOff(key)`.

  They are invoked with `emitEvent("__juce__invoke", {name, params, resultId})` and complete with `"__juce__complete" {promiseId, result}`. Both functions go into the processor's `MidiKeyboardState` on channel 1 and are merged into the next audio block.
- **Event `"telemetry"`:** about 30 Hz, sent only while the editor is visible (`emitEventIfBrowserIsVisible`). The payload is an object with these fields:

| Field | Unit | Meaning |
|-------|------|---------|
| `windSpeed` | m/s | Instantaneous $U(t)$ |
| `windGust` | — | $G(t)$, unit-variance turbulence |
| `gustFactor` | — | $1 + I\,G(t)$, at least 0 |
| `rainRate` | mm/h | Wind-coupled $R(t)$ |
| `lambda` | 1/mm | Marshall-Palmer slope $\Lambda$ |
| `grainRate` | 1/s | Drop arrival rate |
| `meanDropMM` | mm | Mean landing diameter ($0.1 + 1.25/\Lambda$) |
| `impactSpeed` | m/s | Mean impact speed $\lvert\mathbf v\rvert$ |
| `activeVoices` | count | Sounding voices (0 to 64) |
| `strikeCount` | count | Total note-ons so far; a change means a new strike |
| `lastStrikeDistance` | m | $x$ of the latest strike |
| `lastStrikeKey` | MIDI note | Key of the latest strike |
| `lastStrikeVelocity` | 1 – 127 | Velocity of the latest strike |
| `keyLevels` | array[88] | Smoothed level per key (index 0 = A0, MIDI 21), about 0 to 1, not clamped |

- **Window:** the editor opens at 1120 × 740 and can be resized from 900 × 600 to 2000 × 1400. The background behind the web view is `#0b1016`.

---

## Repository layout

```text
Petrichor/
├── CMakeLists.txt            engine library, tests, renderer, JUCE plug-in, frontend embedding
├── Source/
│   ├── DSP/                  JUCE-free C++17 engine (everything audible)
│   │   ├── Atmosphere.h        physical constants and every atmospheric formula
│   │   ├── DspCore.h           RNG, one-pole and TPT SVF filters, ramps, pan law, denormal guard
│   │   ├── KolmogorovNoise.*   time-warped −5/3 turbulence generator
│   │   ├── StormWind.h         storm-wide gusts U(t) = U (1 + I G)
│   │   ├── WindAir.h           audible wind: roar and Aeolian wire whistles
│   │   ├── PianoVoice.*        modal voice: hammer, lightning crack, thunder roll, wind and rain coupling
│   │   ├── PianoHybrid.*       Piano I's measured partials and attack residuals (parsed from the blob)
│   │   ├── Data/piano_hybrid.bin  the Salamander Grand analysed (embedded at build time)
│   │   ├── SympatheticResonance.h  free strings ringing along (I)
│   │   ├── RainTexture.*       wind-coupled Marshall-Palmer granular rain
│   │   └── PetrichorEngine.*   voices and stealing, control rate, mixing, telemetry
│   └── Plugin/               JUCE 8 wrapper
│       ├── Parameters.h        parameter table (single source of truth)
│       ├── PluginProcessor.*   MIDI, block splitting, state, telemetry snapshot
│       └── PluginEditor.*      web view host, relays, native functions, telemetry event
├── frontend/                 React + Vite + Tailwind UI → dist/index.html (git-ignored)
│   └── src/juce.js, params.js  bridge to the JUCE web backend; mirror of Parameters.h
├── Tests/EngineTests.cpp     physics and behaviour tests
└── Tools/
    ├── RenderDemo.cpp        PetrichorRender: demo renders and loudness calibration
    ├── WavWriter.h           24-bit stereo WAV writer
    ├── BinToCpp.cpp          build-time helper: embeds the data blob as C++
    └── hybrid/analyse_salamander.py  the analysis that turns the recordings into Piano I
```

Build directories (`build/`, `build-*/`) and `node_modules/` are git-ignored.

---

## Credits

Piano I is derived from the **Salamander Grand Piano V3** by **Alexander Holm** (Yamaha C5; retuned SFZ by Markus Fiedler, reconstructed by kinwie), licensed under the [Creative Commons Attribution 3.0 Unported licence](https://creativecommons.org/licenses/by/3.0/). `Source/DSP/Data/piano_hybrid.bin` is an adaptation of it: the recordings are not included, only the parameters of a modal model fitted to them and short attack residuals derived from them (see `Tools/hybrid/analyse_salamander.py`).
