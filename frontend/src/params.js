// Mirror of Source/Plugin/Parameters.h (ids, names, units, ranges, defaults, skew centres).
// Parameters.h is authoritative; the backend's "propertiesChanged" event overrides ranges at runtime.

/**
 * @typedef {Object} ParamSpec
 * @property {string} id       relay / parameter id
 * @property {string} name     full name (as in Parameters.h)
 * @property {string} short    compact label used under the knob
 * @property {string} unit     "" means a 0..1 amount shown as percent
 * @property {number} min
 * @property {number} max
 * @property {number} def      default value
 * @property {number} centre   value at the knob's midpoint (skew); 0 / <= min means linear
 * @property {number} [origin] where the value arc starts (bipolar controls)
 * @property {string} group
 * @property {string} desc     one-line physical description
 */

/** @type {ParamSpec[]} */
export const PARAMS = [
  // Piano
  { id: "hammer_hardness", name: "Hammer Hardness", short: "Hammer", unit: "", min: 0, max: 1, def: 0.5, centre: 0, group: "piano",
    desc: "Felt stiffness: a harder hammer has a shorter contact time and excites brighter upper partials." },
  { id: "sustain", name: "Sustain", short: "Sustain", unit: "x", min: 0.3, max: 3, def: 1, centre: 1, group: "piano",
    desc: "Scales every string's ring-down time (T60)." },
  { id: "unison", name: "String Detune", short: "Detune", unit: "ct", min: 0, max: 4, def: 1.2, centre: 0, group: "piano",
    desc: "Mistuning between the strings of a unison: beating and a two-stage decay." },
  { id: "stereo_width", name: "Stereo Width", short: "Width", unit: "", min: 0, max: 1, def: 0.7, centre: 0, group: "piano",
    desc: "Spreads the keys across the stereo field, bass to the left, treble to the right." },
  { id: "piano_level", name: "Piano Level", short: "Level", unit: "dB", min: -24, max: 6, def: 0, centre: 0, origin: 0, group: "piano",
    desc: "Gain of the struck strings." },
  { id: "tuning", name: "Tuning A4", short: "A4", unit: "Hz", min: 415, max: 466, def: 440, centre: 0, origin: 440, group: "piano",
    desc: "Reference pitch: every string is tuned relative to A4." },

  // Thunder
  { id: "storm_distance", name: "Storm Distance", short: "Distance", unit: "m", min: 50, max: 4000, def: 1200, centre: 800, group: "thunder",
    desc: "Where the softest strike (V = 1) lands: x = D\u00B7(127 \u2212 V)/126, so hard hits land overhead." },
  { id: "crack_level", name: "Crack", short: "Crack", unit: "", min: 0, max: 1, def: 0.5, centre: 0, group: "thunder",
    desc: "Level of the near-field crack, the N-wave shock of the lightning channel." },
  { id: "air_absorption", name: "Air Absorption", short: "Air Abs.", unit: "", min: 0, max: 1, def: 0.4, centre: 0, group: "thunder",
    desc: "High-frequency loss over distance, e^(\u2212\u03B1(f)\u00B7x) with \u03B1 \u221D f\u00B2: far strikes thud." },
  { id: "rumble_mix", name: "Rumble", short: "Rumble", unit: "", min: 0, max: 1, def: 0.35, centre: 0, group: "thunder",
    desc: "Multipath rumble: delayed echoes from the tortuous channel and the terrain." },
  { id: "rumble_decay", name: "Rumble Decay", short: "Decay", unit: "s", min: 0.5, max: 12, def: 5, centre: 4, group: "thunder",
    desc: "Length of the rolling tail; distant strikes stretch it further." },

  // Wind
  { id: "wind_speed", name: "Wind Speed", short: "Speed", unit: "m/s", min: 0, max: 30, def: 8, centre: 8, group: "wind",
    desc: "Mean wind U. Each key breathes at its Strouhal rate f = St\u00B7U / L: low keys slow, high keys fast." },
  { id: "turbulence", name: "Turbulence", short: "Turbulence", unit: "", min: 0, max: 1, def: 0.35, centre: 0, group: "wind",
    desc: "Turbulence intensity I: gust factor 1 + I\u00B7G(t), with a von K\u00E1rm\u00E1n / Kolmogorov \u22125/3 spectrum." },
  { id: "gust_length", name: "Gust Length", short: "Gust Len.", unit: "m", min: 2, max: 100, def: 12, centre: 15, group: "wind",
    desc: "Integral length scale L: bigger eddies give slower, broader swells (corner f = U / 8.4L)." },
  { id: "wind_drift", name: "Doppler Drift", short: "Drift", unit: "", min: 0, max: 1, def: 0.35, centre: 0, group: "wind",
    desc: "Doppler micro-pitch drift as gusts carry the sound toward and away from you." },
  { id: "wind_filter", name: "Gust Filter", short: "Filter", unit: "", min: 0, max: 1, def: 0.4, centre: 0, group: "wind",
    desc: "Depth of the resonant band-pass that sweeps with the gusts." },
  { id: "wind_air", name: "Wind Air", short: "Air", unit: "", min: 0, max: 1, def: 0.15, centre: 0, group: "wind",
    desc: "Level of the broadband wind noise itself." },

  // Rain
  { id: "rain_rate", name: "Rain Rate", short: "Rate", unit: "mm/h", min: 0, max: 150, def: 8, centre: 15, group: "rain",
    desc: "Rainfall R. Marshall\u2013Palmer \u039B = 4.1\u00B7R^\u22120.21: heavier rain, more and bigger drops." },
  { id: "rain_coupling", name: "Wind Coupling", short: "Coupling", unit: "", min: 0, max: 1, def: 0.6, centre: 0, group: "rain",
    desc: "How hard gusts drive the rain: R(t) = R\u00B7(1 + I\u00B7G)^3k." },
  { id: "rain_level", name: "Rain Level", short: "Level", unit: "", min: 0, max: 1, def: 0.35, centre: 0, group: "rain",
    desc: "Level of the granular rain bed." },
  { id: "rain_surface", name: "Puddles", short: "Puddles", unit: "", min: 0, max: 1, def: 0.3, centre: 0, group: "rain",
    desc: "Share of drops landing in water: bubble pings at the Minnaert frequency f \u2248 3.26 / a." },

  // Master
  { id: "master", name: "Master", short: "Master", unit: "dB", min: -24, max: 6, def: -2, centre: 0, origin: 0, group: "master",
    desc: "Output gain after the whole storm." },
];

export const PARAM_BY_ID = Object.fromEntries(PARAMS.map((p) => [p.id, p]));

/** Display order of the control deck. `cols` = knob columns inside the group. */
export const GROUPS = [
  { id: "thunder", title: "Thunder", accent: "bolt", cols: 3,
    caption: "Velocity is distance: x ∝ (127 − V)",
    long: "Each hammer strike is a lightning strike. Hard hits crack overhead; soft hits are distant thuds whose highs the air has absorbed, followed by long multipath rumble." },
  { id: "wind", title: "Wind", accent: "mist", cols: 3,
    caption: "Sustain breathes at f = St·U / L",
    long: "Kolmogorov turbulence modulates the sustain. Each key's LFO follows the Strouhal law f = St U / L: low keys sway slowly, high keys flutter." },
  { id: "rain", title: "Rain", accent: "rain", cols: 2,
    caption: "Gusts raise R; Λ = 4.1 R^−0.21",
    long: "Granular Marshall-Palmer rain. Gusts raise the rainfall rate R: more drops, bigger drops, brighter impacts." },
  { id: "piano", title: "Piano", accent: "earth", cols: 3,
    caption: "Stiff strings, felt hammers",
    long: "The instrument itself: hammer felt, unison detune, decay, width and tuning." },
  { id: "master", title: "Master", accent: "mist", cols: 1,
    caption: "Output gain",
    long: "Output gain." },
].map((g) => ({ ...g, params: PARAMS.filter((p) => p.group === g.id) }));

//==============================================================================
// Ranges (JUCE NormalisableRange semantics)

/** JUCE setSkewForCentre: skew = log(0.5) / log((centre - min) / (max - min)). */
export function skewFor(spec) {
  const { min, max, centre } = spec;
  if (!(centre > min && centre < max)) return 1;
  return Math.log(0.5) / Math.log((centre - min) / (max - min));
}

export const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

/** normalised = ((scaled - start) / (end - start)) ^ skew */
export function toNormalised(scaled, range) {
  const { start, end, skew } = range;
  if (!(end > start)) return 0;
  const p = clamp((scaled - start) / (end - start), 0, 1);
  return Math.pow(p, skew);
}

/** scaled = normalised ^ (1 / skew) * (end - start) + start */
export function fromNormalised(normalised, range) {
  const { start, end, skew } = range;
  const n = clamp(normalised, 0, 1);
  return Math.pow(n, 1 / skew) * (end - start) + start;
}

export function snapToInterval(value, range) {
  const { start, end, interval } = range;
  const v = clamp(value, start, end);
  if (!interval) return v;
  return clamp(start + interval * Math.floor((v - start) / interval + 0.5), start, end);
}

export function defaultRange(spec) {
  return { start: spec.min, end: spec.max, skew: skewFor(spec), interval: 0 };
}

//==============================================================================
// Formatting

const MINUS = "−";

function signed(v, decimals) {
  const r = Number(v.toFixed(decimals));
  if (r === 0) return (0).toFixed(decimals);
  return (r > 0 ? "+" : MINUS) + Math.abs(r).toFixed(decimals);
}

/** Human formatting for a parameter value (unitless amounts as percent, dB signed). */
export function formatValue(spec, v) {
  if (!Number.isFinite(v)) return "—";
  switch (spec.unit) {
    case "":
      return `${Math.round(v * 100)}%`;
    case "dB":
      return `${signed(v, 1)} dB`;
    case "Hz":
      return `${v.toFixed(1)} Hz`;
    case "m":
      if (v >= 1000) return `${(v / 1000).toFixed(2)} km`;
      return v < 10 ? `${v.toFixed(1)} m` : `${Math.round(v)} m`;
    case "m/s":
      return `${v.toFixed(1)} m/s`;
    case "mm/h":
      return v < 10 ? `${v.toFixed(1)} mm/h` : `${Math.round(v)} mm/h`;
    case "s":
      return `${v.toFixed(1)} s`;
    case "x":
      return `${v.toFixed(2)}×`;
    case "ct":
      return `${v.toFixed(2)} ct`;
    default:
      return `${v.toFixed(2)} ${spec.unit}`;
  }
}

/** Distances in readouts (m below 1 km, km above). */
export function formatDistance(m) {
  if (!Number.isFinite(m)) return "—";
  return m >= 1000 ? `${(m / 1000).toFixed(2)} km` : `${Math.round(m)} m`;
}

const NOTE_NAMES = ["C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"];

export function noteName(midi) {
  const m = Math.round(midi);
  return `${NOTE_NAMES[((m % 12) + 12) % 12]}${Math.floor(m / 12) - 1}`;
}

export const isBlackKey = (midi) => [1, 3, 6, 8, 10].includes(((midi % 12) + 12) % 12);

export const LOWEST_KEY = 21; // A0
export const HIGHEST_KEY = 108; // C8

/** MIDI velocity as strike distance, mirrors atmos::velocityToDistance: x = xMax (127 - V) / 126. */
export function velocityToDistance(velocity, maxDistance) {
  return maxDistance * clamp((127 - velocity) / 126, 0, 1);
}

export const SPEED_OF_SOUND = 343;
