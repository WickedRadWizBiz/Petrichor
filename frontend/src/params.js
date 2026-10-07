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
  { id: "piano_level", name: "Piano Level", short: "Level", unit: "dB", min: -24, max: 6, def: 0, centre: -100, origin: 0, group: "piano",
    desc: "Gain of the struck strings." },
  { id: "piano_character", name: "Piano I/II", short: "I ↔ II", unit: "", min: 0, max: 1, def: 0, centre: 0, group: "piano", blend: true,
    labels: ["I", "II"], hints: ["Grand", "Tine"],
    desc: "I: a real grand (the Salamander Yamaha C5), resynthesised partial by partial with its measured decays, beats and hammer noise. II: a Rhodes-style tine piano: harmonic, a round fundamental, a bell ping and a bark when struck hard. In between, every partial morphs." },
  { id: "resonance", name: "String Resonance", short: "Resonance", unit: "", min: 0, max: 1, def: 0.5, centre: 0, group: "piano", mode: "I",
    desc: "I: undamped strings ring along with what you play (sympathetic resonance), blooming with the sustain pedal. A tine piano (II) has none." },
  { id: "tuning", name: "Tuning A4", short: "A4", unit: "Hz", min: 415, max: 466, def: 440, centre: 0, origin: 440, group: "piano",
    desc: "Reference pitch: every string is tuned relative to A4." },

  // Thunder
  { id: "storm_distance", name: "Storm Distance", short: "Distance", unit: "m", min: 50, max: 4000, def: 600, centre: 800, group: "thunder",
    desc: "Where the softest strike (V = 1) lands: x = D\u00B7(127 \u2212 V)/126, so hard hits land overhead." },
  { id: "crack_level", name: "Crack", short: "Crack", unit: "", min: 0, max: 1, def: 0.3, centre: 0, group: "thunder",
    desc: "The strike's edge: a broadband crack inside the hammer's force, S(t) = A e^(−t/τ) n(t), heard only through the string's partials, and the hammer's soft re-contacts on distant strikes. Harder strikes crack more." },
  { id: "air_absorption", name: "Air Absorption", short: "Air Abs.", unit: "", min: 0, max: 1, def: 0.35, centre: 0, group: "thunder",
    desc: "How strongly distance darkens the strike: every partial's excitation is weighted by e^(−α(f)·x), α ∝ f²." },
  { id: "rumble_mix", name: "Rumble", short: "Rumble", unit: "", min: 0, max: 1, def: 0.5, centre: 0, group: "thunder",
    desc: "The thunder rolling through the low strings: below middle C, growing toward A0, a few swells darken the note toward thunder's register and lower it by a few cents, with a soft body rumble. Colour, not volume." },
  { id: "rumble_decay", name: "Rumble Decay", short: "Decay", unit: "s", min: 0.5, max: 12, def: 3.5, centre: 4, group: "thunder",
    desc: "How long the thunder rolls through the low strings; distant (soft) strikes roll longer." },

  // Wind
  { id: "wind_speed", name: "Wind Speed", short: "Speed", unit: "m/s", min: 0, max: 30, def: 8, centre: 8, group: "wind",
    desc: "Mean wind U. Each key breathes at its Strouhal rate f = St\u00B7U / L: low keys slow, high keys fast." },
  { id: "turbulence", name: "Turbulence", short: "Turbulence", unit: "", min: 0, max: 1, def: 0.35, centre: 0, group: "wind",
    desc: "Turbulence intensity I: gust factor 1 + I\u00B7G(t), with a von K\u00E1rm\u00E1n / Kolmogorov \u22125/3 spectrum." },
  { id: "gust_length", name: "Gust Length", short: "Gust Len.", unit: "m", min: 2, max: 100, def: 12, centre: 15, group: "wind",
    desc: "Integral length scale L: bigger eddies give slower, broader swells (corner f = U / 8.4L)." },
  { id: "wind_blend", name: "Wind Overlay/Fuse", short: "Overlay ↔ Fuse", unit: "", min: 0, max: 1, def: 0.7, centre: 0, group: "wind", blend: true,
    desc: "Overlay: the wind is heard beside the piano (roar and whistling wires). Fuse: the wind moves inside the notes, bending pitch and timbre like wind bends a whistle." },
  { id: "wind_pitch", name: "Wind Pitch", short: "Pitch", unit: "", min: 0, max: 1, def: 0.35, centre: 0, group: "wind", mode: "fuse",
    desc: "Fuse: each note bends with the wind the way an Aeolian tone does (f ∝ U): pitch ratio (U_key / U)^depth. Stronger wind bends further." },
  { id: "wind_timbre", name: "Wind Timbre", short: "Timbre", unit: "", min: 0, max: 1, def: 0.4, centre: 0, group: "wind", mode: "fuse",
    desc: "Fuse: gusts brighten the note, lulls darken it, and a gentle resonant band-pass sweeps across its harmonics." },
  { id: "wind_air", name: "Air Level", short: "Air", unit: "", min: 0, max: 1, def: 0.3, centre: 0, group: "wind", mode: "overlay",
    desc: "Overlay: level of the audible wind, a roar ∝ U² and wires whistling at f = St·U / D." },

  // Rain
  { id: "rain_rate", name: "Rain Rate", short: "Rate", unit: "mm/h", min: 0, max: 150, def: 8, centre: 15, group: "rain",
    desc: "Rainfall R. Marshall\u2013Palmer \u039B = 4.1\u00B7R^\u22120.21: heavier rain, more and bigger drops." },
  { id: "rain_coupling", name: "Wind Coupling", short: "Coupling", unit: "", min: 0, max: 1, def: 0.6, centre: 0, group: "rain",
    desc: "How hard gusts drive the rain: R(t) = R\u00B7(1 + I\u00B7G)^3k (needs wind)." },
  { id: "rain_level", name: "Rain Level", short: "Level", unit: "", min: 0, max: 1, def: 0.35, centre: 0, group: "rain",
    desc: "Amount of rain texture, in both overlay and fuse." },
  { id: "rain_surface", name: "Puddles", short: "Puddles", unit: "", min: 0, max: 1, def: 0.3, centre: 0, group: "rain",
    desc: "Share of drops landing in water: bubble pings at the Minnaert frequency f \u2248 3.26 / a." },
  { id: "rain_blend", name: "Rain Overlay/Fuse", short: "Overlay ↔ Fuse", unit: "", min: 0, max: 1, def: 0.5, centre: 0, group: "rain", blend: true,
    desc: "Overlay: a rain layer beside the piano that follows its playing and the wind. Fuse: rain inside the notes — drops land on the ringing strings and the patter textures the tone." },
  { id: "rain_follow", name: "Piano Follow", short: "Follow", unit: "", min: 0, max: 1, def: 0.6, centre: 0, group: "rain", mode: "overlay",
    desc: "Overlay: how much the rain layer listens to the piano. Louder playing brings denser, louder drops whose tone leans toward the piano's." },

  // Master
  { id: "master", name: "Master", short: "Master", unit: "dB", min: -24, max: 6, def: -2, centre: -100, origin: 0, group: "master",
    desc: "Output gain after the whole storm." },
];

export const PARAM_BY_ID = Object.fromEntries(PARAMS.map((p) => [p.id, p]));

/** Display order of the control deck. `cols` = knob columns inside the group. */
export const GROUPS = [
  { id: "thunder", title: "Thunder", accent: "bolt", cols: 3,
    caption: "Always fused: the hammer strike is the lightning",
    long: "Velocity is distance, x ∝ (127 − V): hard hits are a bright overhead crack, soft hits arrive darker. Below middle C the thunder then rolls through the low strings, a few swells that darken and slightly lower the note, deepest toward A0, with a soft body rumble: colour, not volume." },
  { id: "wind", title: "Wind", accent: "mist", cols: 3,
    caption: "Fused notes bend like wind pitch, f ∝ U",
    long: "Overlay: hear the wind itself. Fuse: Kolmogorov turbulence bends each note's pitch and timbre the way wind bends a whistle; each key moves at its Strouhal rate f = St U / L, low keys slowly, high keys fast." },
  { id: "rain", title: "Rain", accent: "rain", cols: 3,
    caption: "Gusts raise R; Λ = 4.1 R^−0.21",
    long: "Marshall-Palmer rain driven by the wind's gusts. Overlay: a texture layer beside the piano that follows its playing. Fuse: rain inside the notes, landing on the strings." },
  { id: "piano", title: "Piano", accent: "earth", cols: 4,
    caption: "I: a real grand, resynthesised · II: a Rhodes-style tine",
    long: "The instrument itself. The I ↔ II slider morphs every partial from a real grand, resynthesised from recordings (measured partials, decays, beats and hammer noise, plus sympathetic resonance), to a Rhodes-style tine piano (harmonic, bell ping, bark when struck hard)." },
  { id: "master", title: "Master", accent: "mist", cols: 1,
    caption: "Output gain",
    long: "Output gain." },
].map((g) => ({
  ...g,
  blend: PARAMS.find((p) => p.group === g.id && p.blend) || null,
  params: PARAMS.filter((p) => p.group === g.id && !p.blend),
}));

/** How relevant a mode-specific control is at a given Overlay/Fuse blend (equal-power, as the engine). */
export function modeRelevance(spec, blend) {
  if (!spec.mode) return 1;
  const b = Math.min(1, Math.max(0, blend)) * Math.PI * 0.5;
  if (spec.mode === "I") return 1 - Math.min(1, Math.max(0, blend));
  if (spec.mode === "II") return Math.min(1, Math.max(0, blend));
  return spec.mode === "fuse" ? Math.sin(b) : Math.cos(b);
}

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
