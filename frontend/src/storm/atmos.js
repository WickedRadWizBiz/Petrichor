// JS mirror of Source/DSP/Atmosphere.h, used by the browser mock and the visualiser.

export const G = 9.81; // m/s^2
export const RHO_W = 1000; // kg/m^3
export const RHO_A = 1.225; // kg/m^3
export const CD = 0.5; // drag coefficient of a sphere, Re ~ 10^3
export const V_T_MAX = 9.3; // m/s, flattened drops saturate (Gunn & Kinzer)
export const N0 = 8000; // Marshall-Palmer intercept, m^-3 mm^-1
export const D_MIN = 0.1; // mm
export const D_MAX = 6.0; // mm
export const STROUHAL = 0.2;
export const C_SOUND = 343;

/** Lambda = 4.1 R^-0.21  [1/mm] */
export function marshallPalmerLambda(rainRate) {
  return 4.1 * Math.pow(Math.max(rainRate, 0.01), -0.21);
}

/** v_T(D) = sqrt(4 g rho_w D / (3 rho_a Cd)), D in mm, capped at 9.3 m/s. */
export function terminalVelocity(diameterMM) {
  const d = Math.max(diameterMM, 0) * 1e-3;
  return Math.min(Math.sqrt((4 * G * RHO_W * d) / (3 * RHO_A * CD)), V_T_MAX);
}

/** Drop arrival flux F = integral N(D) v_T(D) dD over [D_MIN, D_MAX]  [drops m^-2 s^-1]. */
export function dropNumberFlux(rainRate) {
  if (rainRate <= 0) return 0;
  const lambda = marshallPalmerLambda(rainRate);
  const n = 32;
  const h = (D_MAX - D_MIN) / n;
  let sum = 0;
  for (let i = 0; i <= n; i++) {
    const d = D_MIN + h * i;
    const f = N0 * Math.exp(-lambda * d) * terminalVelocity(d);
    const w = i === 0 || i === n ? 1 : i & 1 ? 4 : 2;
    sum += w * f;
  }
  return (sum * h) / 3;
}

/** Exponential diameter with rate lambda, truncated to [D_MIN, D_MAX] (inverse CDF). */
export function sampleDiameter(lambda, u = Math.random()) {
  const span = D_MAX - D_MIN;
  const tail = 1 - Math.exp(-lambda * span);
  return D_MIN - Math.log(1 - u * tail) / lambda;
}

/** Strouhal shedding / breathing rate f = St U / L, with L = c / f0 for a key. */
export function keyBreathHz(midiKey, windSpeed) {
  const f0 = 440 * Math.pow(2, (midiKey - 69) / 12);
  return (STROUHAL * windSpeed * f0) / C_SOUND;
}
