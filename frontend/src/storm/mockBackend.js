// A small storm simulation that stands in for the C++ engine when the page runs in a plain browser.
// It publishes the same "telemetry" shape as PluginEditor::timerCallback().

import { D_MIN, dropNumberFlux, keyBreathHz, marshallPalmerLambda, terminalVelocity } from "./atmos";
import { velocityToDistance, LOWEST_KEY, HIGHEST_KEY, clamp } from "../params";

const KEY_COUNT = HIGHEST_KEY - LOWEST_KEY + 1;
const COLLECTOR_AREA = 0.015; // m^2, as RainTexture::kCollectorArea

function gaussian() {
  let u = 0;
  while (u === 0) u = Math.random();
  return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * Math.random());
}

/**
 * @param {{ getValue: (id: string) => number, publish: (telemetry: object) => void, hz?: number }} opts
 */
export function createMockBackend({ getValue, publish, hz = 30 }) {
  // von Karman-ish gust G(t): three Ornstein-Uhlenbeck processes whose time constants step down by 3x.
  // Weighting their variances by tau^(2/3) approximates the Kolmogorov -5/3 slope above the corner.
  const TAU_STEP = 3;
  const octaves = [0, 1, 2];
  const rawWeights = octaves.map((k) => Math.pow(TAU_STEP, -k / 3));
  const weightNorm = Math.sqrt(rawWeights.reduce((s, w) => s + w * w, 0));
  const weights = rawWeights.map((w) => w / weightNorm); // unit variance overall
  const ou = octaves.map(() => gaussian());

  let meanU = getValue("wind_speed");
  let rain = getValue("rain_rate");
  let lambda = marshallPalmerLambda(rain);

  const keys = new Map(); // midi -> { level, held, phase }
  let strikeCount = 0;
  let lastStrikeDistance = 0;
  let lastStrikeKey = 0;
  let lastStrikeVelocity = 0;
  let last = performance.now();

  function tick() {
    const now = performance.now();
    const dt = clamp((now - last) / 1000, 0.001, 0.25);
    last = now;

    // Wind: U(t) = U_mean (1 + I G(t)), corner at U / (8.41 L) (Taylor's frozen turbulence).
    meanU += (getValue("wind_speed") - meanU) * (1 - Math.exp(-dt / 0.35));
    const intensity = 0.6 * clamp(getValue("turbulence"), 0, 1);
    const L = Math.max(getValue("gust_length"), 0.5);
    const corner = Math.max(meanU, 1) / (L * 8.414);
    const tau0 = 1 / (2 * Math.PI * corner);
    let g = 0;
    for (let k = 0; k < ou.length; k++) {
      const tau = tau0 / Math.pow(TAU_STEP, k);
      const decay = Math.exp(-dt / tau);
      ou[k] = ou[k] * decay + Math.sqrt(1 - decay * decay) * gaussian();
      g += weights[k] * ou[k];
    }
    const gustFactor = Math.max(0, 1 + intensity * g);
    const windSpeed = meanU * gustFactor;

    // Rain: R(t) = R (1 + I G)^(3k), smoothed over 250 ms.
    const coupling = clamp(getValue("rain_coupling"), 0, 1);
    const target = Math.max(getValue("rain_rate"), 0) * Math.pow(gustFactor, 3 * coupling);
    rain += (target - rain) * (1 - Math.exp(-dt / 0.25));

    let grainRate = 0;
    let meanDropMM = 0;
    let impactSpeed = 0;
    if (rain >= 0.01 && getValue("rain_level") > 0) {
      lambda = marshallPalmerLambda(rain);
      grainRate = dropNumberFlux(rain) * COLLECTOR_AREA;
      meanDropMM = D_MIN + 1.25 / lambda;
      impactSpeed = Math.hypot(terminalVelocity(meanDropMM), windSpeed);
    }

    // Keys: ring down with a key-dependent T60 scaled by Sustain, breathe at the Strouhal rate.
    const sustain = getValue("sustain");
    const drift = clamp(getValue("wind_drift"), 0, 1);
    const keyLevels = new Array(KEY_COUNT).fill(0);
    let activeVoices = 0;
    for (const [key, s] of keys) {
      const pos = clamp((key - LOWEST_KEY) / (KEY_COUNT - 1), 0, 1);
      const t60 = (0.7 + 11 * Math.pow(1 - pos, 1.7)) * sustain;
      const tau = s.held ? t60 / 6.9 : 0.14;
      s.level *= Math.exp(-dt / tau);
      if (s.level < 0.004) {
        keys.delete(key);
        continue;
      }
      activeVoices++;
      s.phase += 2 * Math.PI * Math.min(keyBreathHz(key, windSpeed), 7) * dt;
      const breath = 1 + (0.15 + 0.35 * drift) * gustFactor * 0.35 * Math.sin(s.phase);
      const idx = key - LOWEST_KEY;
      if (idx >= 0 && idx < KEY_COUNT) keyLevels[idx] = clamp(s.level * breath, 0, 1);
    }

    publish({
      windSpeed,
      windGust: g,
      gustFactor,
      rainRate: rain,
      lambda,
      grainRate,
      meanDropMM,
      impactSpeed,
      activeVoices,
      strikeCount,
      lastStrikeDistance,
      lastStrikeKey,
      lastStrikeVelocity,
      keyLevels,
    });
  }

  const timer = setInterval(tick, 1000 / hz);
  tick();

  return {
    noteOn(key, velocity) {
      keys.set(key, { level: 0.35 + 0.65 * (velocity / 127), held: true, phase: Math.random() * 2 * Math.PI });
      strikeCount++;
      lastStrikeKey = key;
      lastStrikeVelocity = velocity;
      lastStrikeDistance = velocityToDistance(velocity, getValue("storm_distance"));
    },
    noteOff(key) {
      const s = keys.get(key);
      if (s) s.held = false;
    },
    dispose() {
      clearInterval(timer);
    },
  };
}
