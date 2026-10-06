# Petrichor Piano: Developer Guidelines and Physics Reference

This file is for agents working in this repo. Petrichor Piano is a modal piano inside a physically modelled storm:

- **Thunder is the hammer strike** (always fused): velocity is distance; the crack, the exact per-partial air absorption and the multipath roll all live in the force the string receives. There is no separate thunder sound.
- **Wind** has an Overlay ↔ Fuse blend: overlay is the audible wind (roar, Aeolian wires); fuse bends each note's pitch like an Aeolian tone ($f \propto U$) at its Strouhal tempo and tilts its timbre.
- **Rain** has an Overlay ↔ Fuse blend: overlay is a granular Marshall-Palmer layer that follows the wind and the piano; fuse lands drops on the ringing strings, excites their modes with hiss and modulates the tone with the patter.

The README has the full mathematics, the parameter and MIDI tables, and the build guide. The earlier "Iso-Storm" design (Triad, hygroscopic wood, B×100 thunder, `native.setParameter`) is obsolete. Do not reintroduce it.

## 1. Core directives

1. **Keep the engine JUCE-free.** Everything audible lives in `Source/DSP/` as plain C++17 and builds with `-DPETRICHOR_ENGINE_ONLY=ON`. Never include JUCE headers there. `Source/Plugin/` is a thin wrapper.
2. **Keep the engine real-time safe.**
   - Allocate only in `prepare()`. After that: no allocation, no locks, no I/O, no exceptions on the audio path. Use fixed-size arrays (`kMaxVoices` = 64, `kMaxGrains` = 512, `kMaxPartials` = 96); the per-voice contact buffers are `std::vector`s sized from the sample rate in `PianoVoice::prepare()`, and the voices themselves live in a heap `std::vector` (each carries its buffers).
   - Seed RNGs deterministically, so a render is bit-identical across runs.
   - Make audio-path functions `noexcept`.
3. **Route physics through `Source/DSP/Atmosphere.h`.**
   - Atmospheric formulas and constants (drop size distribution, terminal velocity, advection, flux, Minnaert, Strouhal, von Kármán, Doppler, absorption, velocity → distance) live in `petrichor::atmos`. Call those functions; never inline a copy.
   - String-model laws are static members of `PianoVoice` (`inharmonicity`, `stretchCents`, `promptT60`).
   - New physics goes into one of these places, with units in the comment.
4. **Keep the UI fully offline.** The UI ships as **one self-contained `frontend/dist/index.html`** (built by `vite-plugin-singlefile`), with scripts, styles and fonts inlined. Fonts come from npm packages such as `@fontsource/*`. The editor's resource provider serves only `/` and `/index.html`. No CDN, no network fetches, no extra files.
5. **Treat `Source/Plugin/Parameters.h` as the single source of truth for parameters.** It defines ids, names, ranges, defaults and skew centres. `frontend/src/params.js` must mirror it exactly. A parameter id is a saved-state and automation key (`ParameterID{id, 1}`), so never rename or reuse one.
6. **Run the engine tests before committing.** The suite must report `0 failures`.
7. **Listen and measure with `PetrichorRender`.** Render the demos before and after any change that affects sound. Renders are deterministic, so you can diff them.
8. **Keep loudness calibrated.** After any change that affects level, run `PetrichorRender --calibrate`. The current per-key profile at V = 80 is flat at about −32 ± 1.5 dB from A0 to C8. The trim lives in `keyLoudnessTrim()` in `PianoVoice.cpp`. The master stage has a soft knee above 0.8, and the stability test requires peak ≤ 1.0.

## 2. Engine conventions

- **Control rate:** `PetrichorEngine::kControlBlock` = 32 samples. Wind, rain, rumble and per-voice modulation update in `controlTick()`; audio is rendered in chunks of at most 32 samples.
- **Event timing:** the engine applies events between `process()` calls. The plug-in splits each host block at MIDI timestamps, so events land on the right sample. A note that steals a voice is queued and starts on the first control tick after the voice's 4 ms fade.
- **Parameter flow:** `Parameters.h` `Spec` → member pointer into `EngineParams` (physical units) → `PetrichorEngine::setParams()` once per block.
- **Adding a parameter:**
  1. Add the field to `EngineParams`.
  2. Add a `Spec` row, and bump the `std::array` size.
  3. Wire the field into the engine.
  4. Mirror it in `frontend/src/params.js`.
  5. Add a row to the README parameter table.
- **Skew:** `rangeFor()` skews whenever `centre` is strictly inside (min, max). Use a centre ≤ min (the dB controls use −100) for a linear range that contains 0.
- **Overlay ↔ Fuse:** blends are equal-power (`overlayAmount()` / `fuseAmount()` in `PetrichorEngine.cpp`: $\cos$ / $\sin$ of $b\pi/2$). Mode-specific controls carry `mode: "overlay" | "fuse"` in `params.js` and fade in the UI as they become irrelevant.
- **Fused effects must scale from open-loop references, never from live energy.** Fused rain once scaled by the string's live energy; drops and hiss then fed on their own energy and grew without bound (NaN at extreme settings). It now scales from `PianoVoice::referenceEnergy`, the hammer's analytically expected energy, decaying at the slowest mode rate and with the damper, and drop power is normalised by drop rate. Keep any new fused behaviour open-loop in the same way.
- **Adding telemetry:**
  1. Add the field to `EngineTelemetry`.
  2. Add an atomic to `TelemetrySnapshot` and store it in `publishTelemetry()`.
  3. Set the property in `PluginEditor::timerCallback()`.
  4. Document it in the README.
- **Voice stealing:** a free voice is used first. Otherwise the voice with the lowest kinetic energy, $\sum\lvert y\rvert^2$ over its modes, is stolen. Released and pedal-sustained voices go before held ones. Re-striking a sounding key adds to the existing voice.
- **Optimisation:** the modal bank is a structure-of-arrays laid out in 8 lanes for auto-vectorisation. The engine is compiled `-O3` even in Debug. Keep inner loops branch-free.

## 3. Formula reference

| Model | Formula | Constants in code | Where |
|-------|---------|-------------------|-------|
| Drop size distribution | $N(D)=N_0e^{-\Lambda D}$, $\Lambda=4.1R^{-0.21}$ | $N_0$ = 8000 m⁻³mm⁻¹, D from 0.1 to 6 mm | `atmos::marshallPalmerLambda/Density` |
| Terminal velocity | $v_T=\sqrt{4g\rho_wD/(3\rho_aC_d)}$ | g 9.81, ρw 1000, ρa 1.225, Cd 0.5, capped at 9.3 m/s | `atmos::terminalVelocity` |
| Advection | $v=\sqrt{v_T^2+U^2}$, $\sin\theta=U/v$ | used for grain pitch and pan | `atmos::impactSpeed`, `slantSine` |
| Drop flux → grains | $F=\int N v_T\,dD$, rate $=F\,A_c$ | Simpson's rule, 32 intervals; $A_c$ 0.015 m²; Poisson arrivals | `atmos::dropNumberFlux`, `RainTexture::controlTick/render` |
| Landing diameter | $p(D)\propto N(D)v_T(D)$ | inverse CDF plus rejection on $v_T/9.3$ | `atmos::sampleImpactDiameter` |
| Rain–wind coupling | $R=R_0(1+\min(\bar U/8,1)(g-1))^{3c}$ | smoothed, τ 0.25 s | `RainTexture::controlTick` |
| Rain overlay follow | $F=(1-\phi)+\phi\min(e,1.5)$; spectra lean to $f_\mathrm{piano}$ by $0.5\phi\min(e,1)$ | e = piano RMS / 0.04 (10/300 ms) | `PetrichorEngine::controlTick`, `RainTexture` |
| Rain fuse | drops: $a=s_d\sqrt{P/\lambda_g}$, $P=2\varphi\ell\min(\rho,2)$; hiss $h=0.3\varphi\ell\min(\rho,2)$; patter AM $1+1.5\varphi\ell p$ (0.3–1.7) | all × open-loop $E_\mathrm{ref}$ | `landDrops`, `PianoVoice::rainDrop/controlTick`, `renderBlock` |
| Grain | $f_c=700v^{0.75}$, $A\propto[(D/2)^{1.5}v/6.5]^{0.7}$, $\tau$=0.4+1.6D ms | band-pass Q 1.3, centre ×0.8 to ×1.2 | `RainTexture::spawnGrain` |
| Minnaert bubble | $f=3.26/a$ | a = 0.25 to 0.6 D; P = surface × (0.45 if D > 1 mm, else 0.1) | `atmos::minnaertFrequency` |
| Kolmogorov noise | $\lvert H\rvert^2\propto f^{-5/3}$ above $f_c$ | 3 pole/zero pairs plus a pole at 12 fc; 128 steps per corner-Hz; time-warped and Hermite-interpolated | `KolmogorovNoise` |
| Storm gusts | $U(t)=\bar U(1+IG)$, corner $\bar U/(8.41L_u)$ | I = 0.6 × turbulence; mean glide τ 0.35 s | `atmos::vonKarmanCornerHz`, `StormWind::tick` |
| Note LFO (Strouhal) | $f=St\,U/L$, $L=c/f_0$ | St 0.2, c 343 m/s, clamped 0.02 to 16 Hz | `atmos::strouhalFrequency`, `noteObstacleLength`, `PianoVoice::controlTick` |
| Wind pitch (fuse) | $f'/f=(U_\mathrm{key}/\bar U)^\delta$, $U_\mathrm{key}=\bar U g(1+I\ell)$ | $\delta=0.06\,\mathrm{pitch}\,\varphi_w\min(\bar U/8,2)$; total ±3 % | `PianoVoice::controlTick`, `applyPitchRatio` |
| Wind timbre (fuse) | per-mode weight, Q 1.6, centre $4f_1\,2^{1.2s(0.55\ell+0.45G)}$, tilt $(f/f_1)^{1.5\,\mathrm{timbre}\,\varphi_w\ln(U_\mathrm{key}/\bar U)}$ | ±15 % swell; tilt ±0.6 | `PianoVoice::updateWeights` |
| Aeolian wires | $f=St\,U/D_w$ | Dw 2.5, 4.0, 6.5 mm; Q 28; fade-in from 3 to 10 m/s | `WindAir` |
| Velocity → distance | $x=x_{max}(127-V)/126$ | $x_{max}$ = `storm_distance` | `atmos::velocityToDistance` |
| Crack (in the force) | $S=Ae^{-t/\tau}n(t)$, $A=0.35\,\mathrm{crack}\,v^{2.5}/\sqrt{\tau f_s/2}$ | τ 10 → 4 ms; only a 70–160 Hz knock is heard directly | `PianoVoice::buildForce` |
| Multipath contact | $F=(F_\mathrm{felt}+S)*\sum_k g_k\mathrm{LP}_k\delta(t-t_k)$ | $K=1+\mathrm{round}(7\mu)$, $\mu=d\cdot$rumble; spacing $(\beta/f_1+4\,\mathrm{ms}\,\mu)U(0.6,1.4)$, ≤ 40 ms | `PianoVoice::buildForce` |
| Absorption | $e^{-\alpha(f_n)xa}$ on every partial's drive, $\alpha=\alpha_{1k}(f/1k)^2$ | α1k = 4.6e-4 Np/m; a = `air_absorption` | `atmos::absorptionGain`, `PianoVoice::strike` |
| Rumble | $R=\int S(t-\tau)h(\tau,t)d\tau$ | 3 zones × (12 rolling taps + 8-line Householder FDN); RT60 near max(0.25, 0.15T), mid 0.45T, far T | `MultipathRumble` |
| Rumble send | send $=\mathrm{mix}(0.12+0.88d)$, triangular equal-power zone weights | $d=(127-V)/126$ | `PetrichorEngine::renderBlock`, `MultipathRumble::zoneWeights` |
| Stiff string | $f_n=nf_0\sqrt{1+Bn^2}$ | B 3e-4 (A0) → 1e-4 (A2) → 8e-3 (C8); Railsback stretch | `PianoVoice::inharmonicity`, `stretchCents` |
| String loss | $\sigma_n=\sigma_1+b_3f_n^2$ | T60 = 14 s × 0.045^(k−21)/87; b3 2.6e-7; aftersound 0.22σ at level 0.32 | `PianoVoice::promptT60`, `strike` |
| Hammer | $F\propto t^{\kappa-1}e^{-t/\theta}$ | speed 0.35·17^v m/s; corner 580 Hz (C4, mf, h = 0.5) → 2.5 kHz tip at II; κ = 2.6 − 0.9v → 2; 0 to 1 ms jitter | `PianoVoice::strike`, `buildForce` |
| Piano I ↔ II | $f_n=f^{\mathrm I}_n(f^{\mathrm{II}}_n/f^{\mathrm I}_n)^c$, $g_n=(1-c)\hat g^{\mathrm I}_n+c\hat g^{\mathrm{II}}_n$, $\sigma_n$ geometric | II: $\hat g\propto b^{n-1}/\sqrt n$, $b=0.16+0.5v^{1.5}+0.12(1-k_n)$; bell 6.9 f1, T60 0.35 s; tine T60 10 s·0.25^k | `PianoVoice::strike` |
| Sympathetic resonance | 24 tuned combs A1–G♯3, loss LP 2.6 kHz | RT60 0.5 s (pedal up) → 3.5 s (down); gain 0.2 × resonance × (1 − c) | `SympatheticResonance` |

## 4. IPC contract (frontend ↔ backend)

The editor is defined in `Source/Plugin/PluginEditor.h/.cpp`; the JS bridge lives in `frontend/src/juce.js`.

- **Parameters:**
  - Each parameter has a `juce::WebSliderRelay` named by its **parameter id**, attached through `WebSliderParameterAttachment`. Relay names are in `__juce__sliders`.
  - The relays use JUCE 8's slider protocol on the event `"__juce__slider" + id`:
    - **UI → plug-in:** `requestInitialUpdate`, `valueChanged` (scaled value), `sliderDragStarted` / `sliderDragEnded`.
    - **Plug-in → UI:** `valueChanged` and `propertiesChanged` (`start`, `end`, `skew`, …).
  - Never add a parameter path that bypasses the relays.
- **Native functions:**
  - `noteOn(key 0–127, velocity 1–127)` and `noteOff(key)`, called via `__juce__invoke` / `__juce__complete`.
  - Both feed `MidiKeyboardState` on channel 1, which is merged into the next block.
- **Event `"telemetry"`:**
  - Sent at about 30 Hz via `emitEventIfBrowserIsVisible`.
  - Fields: `windSpeed`, `windGust`, `gustFactor`, `rainRate`, `lambda`, `grainRate`, `meanDropMM`, `impactSpeed`, `activeVoices`, `strikeCount` (increments on every note-on), `lastStrikeDistance`, `lastStrikeKey`, `lastStrikeVelocity`, and `keyLevels` (88 floats, where index 0 is A0).
  - The README lists units for each field.
- **Window:** the editor opens at 1120 × 740 and resizes from 900 × 600 to 2000 × 1400.

## 5. Build and verify

```sh
# Engine, tests and renderer: no JUCE, no npm
cmake -S . -B build-engine -DPETRICHOR_ENGINE_ONLY=ON && cmake --build build-engine --parallel
./build-engine/PetrichorTests                      # must print "0 failures"
mkdir -p renders && ./build-engine/PetrichorRender renders
./build-engine/PetrichorRender --calibrate

# Full plug-in (VST3, AU on macOS, Standalone)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release [-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE-8.0.9]
cmake --build build --config Release --parallel
```

- **Frontend build:** CMake builds the frontend with `npm ci --include=dev` and `npm run build` when the frontend sources change. `juce_add_binary_data(PetrichorFrontend)` then embeds `frontend/dist/index.html`. Without npm, CMake falls back to an existing `dist/index.html`. `frontend/dist`, `node_modules` and `build*/` are git-ignored.
- **Windows:** always uses WebView2 (the editor's resource provider needs it), which requires the `Microsoft.Web.WebView2` NuGet package.
- **Linux:** needs the JUCE development packages listed in the README, including `libwebkit2gtk-4.1-dev` and `libgtk-3-dev`.
- **MSVC:** `Tests/EngineTests.cpp` defines `_USE_MATH_DEFINES` for `M_PI`; keep engine code on `kPi` / `kTwoPi`.
- **README sync:** whenever you change a constant quoted in the README, update the README as well.
