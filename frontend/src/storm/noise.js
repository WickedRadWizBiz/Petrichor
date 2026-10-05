// 3-D value noise and the fractional Brownian motion used for the gust sheets.
//
//   W(x, y, t) = sum_i a^i Noise(b^i x, b^i y, c^i t)
//
// b = 2 (lacunarity), a = 2^(-1/3) (persistence): amplitude ~ k^(-H) with Kolmogorov's H = 1/3,
// i.e. the -5/3 energy spectrum of inertial-range turbulence. Small eddies also turn over faster
// (tau ~ l^(2/3)), so each octave's time axis runs c = 2^(2/3) times quicker.

export const LACUNARITY = 2;
export const PERSISTENCE = Math.pow(2, -1 / 3);
export const TIME_RATIO = Math.pow(2, 2 / 3);

function mulberry32(seed) {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

const perm = new Uint8Array(512);
const values = new Float32Array(256);
{
  const rnd = mulberry32(0x9e7a1c0d);
  const p = new Uint8Array(256);
  for (let i = 0; i < 256; i++) {
    p[i] = i;
    values[i] = rnd() * 2 - 1;
  }
  for (let i = 255; i > 0; i--) {
    const j = Math.floor(rnd() * (i + 1));
    const tmp = p[i];
    p[i] = p[j];
    p[j] = tmp;
  }
  for (let i = 0; i < 512; i++) perm[i] = p[i & 255];
}

const fade = (t) => t * t * t * (t * (t * 6 - 15) + 10);
const lerp = (a, b, t) => a + (b - a) * t;

/** Smooth value noise in [-1, 1]. */
export function noise3(x, y, z) {
  const xf0 = Math.floor(x);
  const yf0 = Math.floor(y);
  const zf0 = Math.floor(z);
  const X = xf0 & 255;
  const Y = yf0 & 255;
  const Z = zf0 & 255;
  const u = fade(x - xf0);
  const v = fade(y - yf0);
  const w = fade(z - zf0);

  const A = perm[X] + Y;
  const AA = perm[A] + Z;
  const AB = perm[A + 1] + Z;
  const B = perm[X + 1] + Y;
  const BA = perm[B] + Z;
  const BB = perm[B + 1] + Z;

  return lerp(
    lerp(lerp(values[perm[AA]], values[perm[BA]], u), lerp(values[perm[AB]], values[perm[BB]], u), v),
    lerp(lerp(values[perm[AA + 1]], values[perm[BA + 1]], u), lerp(values[perm[AB + 1]], values[perm[BB + 1]], u), v),
    w
  );
}

/** Kolmogorov fBm, normalised by the amplitude sum (roughly within [-0.7, 0.7]). */
export function fbm3(x, y, t, octaves = 5) {
  let sum = 0;
  let norm = 0;
  let amp = 1;
  let fs = 1;
  let ft = 1;
  for (let i = 0; i < octaves; i++) {
    // Offset each octave so their lattices do not line up at the origin.
    sum += amp * noise3(x * fs + i * 19.19, y * fs + i * 7.31, t * ft + i * 3.7);
    norm += amp;
    amp *= PERSISTENCE;
    fs *= LACUNARITY;
    ft *= TIME_RATIO;
  }
  return sum / norm;
}

/** 1-D helper for static silhouettes. */
export function fbm1(x, octaves = 4) {
  return fbm3(x, 0.37, 0.11, octaves);
}

export { mulberry32 };
