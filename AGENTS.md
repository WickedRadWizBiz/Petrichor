# Petrichor / Iso-Storm - Developer Guidelines & Physics Reference

## 1. Core Directives

*   **Architecture**: "The Triad" - The Cloud (Generator), The Wood (Modifier), The Wire (Resonator).
*   **Offline Requirement**: All frontend assets (React, Tailwind, Fonts) must be bundled into the binary. No external CDN calls.
*   **Performance**: Support 500+ voices using SIMD. Use Voice Stealing based on Kinetic Energy.

## 2. Physics Formulas & Constraints

### 2.1 The Cloud (Raindrop Acoustics)
*   **Minnaert Resonance (Pitch)**:
    $$f \approx \frac{3.26}{a}$$
    Where $a$ is radius in meters.
    *   Small drops ($<0.5mm$) -> High Pitch (C7-C8).
    *   Large drops ($>4.0mm$) -> Low Pitch (C2-C4).

*   **Marshall-Palmer Distribution (Drop Probability)**:
    $$N(D) = N_0 e^{-\Lambda D}$$
    $$\Lambda = 41 R^{-0.21}$$
    Where $R$ is Rainfall Rate (mm/hr).
    *   Maps to MIDI Velocity. Larger D = Higher Velocity.

*   **Poisson Distribution (Timing)**:
    $$P(k) = \frac{(\lambda t)^k e^{-\lambda t}}{k!}$$
    *   Used for granular triggering. Implement "Burst Generators" for patchiness.

### 2.2 The Wire (Wind & String Physics)
*   **Aeolian Tones (Wind)**:
    $$f_{Aeolian} = \frac{St \cdot V}{D_{wire}}$$
    *   $St \approx 0.2$.
    *   If $f_{Aeolian}$ matches longitudinal mode $f_L$, resonance occurs.

*   **Longitudinal Mode**:
    $$f_L = \frac{n c_L}{2L}$$
    *   $c_L \approx 5000 m/s$ for steel.

*   **Inharmonicity (Stiffness)**:
    $$f_n = n f_0 \sqrt{1 + B n^2}$$
    *   $B$ (Inharmonicity Coefficient) is the target for Thunder simulation.

### 2.3 The Wood (Hygroscopic & Thunder)
*   **Thunder Generation**:
    *   Trigger: Bass notes (A0-B0).
    *   Action: Instantly increase $B$ by 100x.
    *   Envelope: N-Wave (Instant attack, linear decay).

*   **Sorption Hysteresis**:
    *   Moisture absorption is slower than desorption.
    *   High MC% = Sharp Pitch + High Damping (Dull Tone).

## 3. Implementation Details

*   **IPC**:
    *   React -> C++: `native.setParameter('rain_intensity', val)`
    *   C++ -> React: `emitEventIfBrowserIsVisible` (for visualizer)
*   **Build**:
    *   Frontend builds to `frontend/dist`.
    *   CMake bundles `frontend/dist` via `juce_add_binary_data`.
