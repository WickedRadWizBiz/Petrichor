// Canvas storm: Marshall-Palmer rain streaks advected by gusty wind, fBm gust sheets,
// lightning whose brightness follows strike distance and time-compressed thunder wavefronts.

import { fbm1, fbm3 } from "./noise";
import { C_SOUND, D_MAX, D_MIN, marshallPalmerLambda, sampleDiameter, terminalVelocity } from "./atmos";
import { clamp, LOWEST_KEY, HIGHEST_KEY } from "../params";

const smoothstep = (a, b, x) => {
  const t = clamp((x - a) / (b - a), 0, 1);
  return t * t * (3 - 2 * t);
};
const lerp = (a, b, t) => a + (b - a) * t;

/** Thunder travels at 343 m/s; the picture runs it this many times faster so you can follow it. */
export const SOUND_TIME_COMPRESSION = 5;

const EXPOSURE = 0.045; // s of motion blur per streak
const STREAK_BUCKETS = 6;

export class StormScene {
  /**
   * @param {{ bgCanvas: HTMLCanvasElement, mistCanvas: HTMLCanvasElement, reducedMotion?: boolean }} opts
   * The static sky and the low-resolution gust field live in their own (CSS-stretched) canvases so the
   * per-frame canvas only carries rain, lightning and rings.
   */
  constructor({ bgCanvas, mistCanvas, reducedMotion = false }) {
    this.reducedMotion = reducedMotion;
    this.cap = reducedMotion ? 450 : 1500;
    this.W = 0;
    this.H = 0;
    this.dpr = 1;
    this.t = 0;

    // Particles (structure of arrays).
    const n = 1500;
    this.count = 0;
    this.px = new Float32Array(n);
    this.py = new Float32Array(n);
    this.vx = new Float32Array(n);
    this.vy = new Float32Array(n);
    this.pd = new Float32Array(n); // diameter, mm
    this.pz = new Float32Array(n); // depth scale 0.35 (far) .. 1 (near)
    this.pland = new Float32Array(n); // ground y where the drop lands
    this.spawnWait = 0;
    this.seeded = false;

    this.splashes = [];
    this.strikes = [];

    // Smoothed telemetry.
    this.U = 0;
    this.gust = 0;
    this.grainRate = 0;
    this.lambda = 2.6;
    this.rain = 0;
    this.advect = 0;

    // fBm gust field on a coarse grid; also samples local wind for the streaks.
    this.gw = 0;
    this.gh = 0;
    this.field = null;
    this.mistCanvas = mistCanvas;
    this.mistCtx = mistCanvas.getContext("2d");
    this.mistImage = null;

    this.bgCanvas = bgCanvas;
    this.puddleCanvas = document.createElement("canvas");
    this.flashLevel = 0;
    this.rumbleGlow = 0;
  }

  get horizonY() {
    return Math.round(this.H * 0.8);
  }

  /** Pixels per metre on the nearest depth plane. */
  get pxPerM() {
    return this.H / 6.5;
  }

  resize(W, H, dpr) {
    if (W === this.W && H === this.H && dpr === this.dpr) return;
    const scaleX = this.W > 0 ? W / this.W : 1;
    const scaleY = this.H > 0 ? H / this.H : 1;
    for (let i = 0; i < this.count; i++) {
      this.px[i] *= scaleX;
      this.py[i] *= scaleY;
      this.pland[i] *= scaleY;
    }
    this.W = W;
    this.H = H;
    this.dpr = dpr;

    // ~9 px cells, at most ~9000 cells (updateField stops octaves before they alias).
    const cell = Math.max(9, Math.sqrt((W * H) / 9000));
    this.gw = clamp(Math.round(W / cell), 32, 220);
    this.gh = clamp(Math.round(H / cell), 10, 120);
    this.field = new Float32Array(this.gw * this.gh);
    this.mistCanvas.width = this.gw;
    this.mistCanvas.height = this.gh;
    this.mistImage = this.mistCtx.createImageData(this.gw, this.gh);

    this.renderBackground();
  }

  renderBackground() {
    const { W, H, dpr } = this;
    const hy = this.horizonY;
    for (const c of [this.bgCanvas, this.puddleCanvas]) {
      c.width = Math.max(1, Math.round(W * dpr));
      c.height = Math.max(1, Math.round(H * dpr));
    }

    // Sky, horizon and wet ground.
    const g = this.bgCanvas.getContext("2d");
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    const sky = g.createLinearGradient(0, 0, 0, hy);
    sky.addColorStop(0, "#04070d");
    sky.addColorStop(0.55, "#0a1321");
    sky.addColorStop(1, "#16263b");
    g.fillStyle = sky;
    g.fillRect(0, 0, W, hy + 1);

    // A faint warm band where the storm meets the land: petrichor light.
    const warm = g.createLinearGradient(0, hy - H * 0.12, 0, hy + 2);
    warm.addColorStop(0, "rgba(212,163,115,0)");
    warm.addColorStop(1, "rgba(212,163,115,0.07)");
    g.fillStyle = warm;
    g.fillRect(0, hy - H * 0.12, W, H * 0.12 + 2);

    // Distant ridge silhouette.
    g.beginPath();
    g.moveTo(0, hy + 1);
    for (let x = 0; x <= W; x += 6) {
      const n = fbm1(x / (W * 0.18) + 3.1);
      g.lineTo(x, hy - H * (0.018 + 0.05 * (n + 0.5)));
    }
    g.lineTo(W, hy + 1);
    g.closePath();
    g.fillStyle = "#0b1422";
    g.fill();

    const ground = g.createLinearGradient(0, hy, 0, H);
    ground.addColorStop(0, "#0b121d");
    ground.addColorStop(1, "#05080e");
    g.fillStyle = ground;
    g.fillRect(0, hy, W, H - hy);

    // Horizon line sheen.
    g.fillStyle = "rgba(160,200,225,0.08)";
    g.fillRect(0, hy, W, 1);

    // Puddles: pale glints that only show when lightning lights the ground.
    const p = this.puddleCanvas.getContext("2d");
    p.setTransform(dpr, 0, 0, dpr, 0, 0);
    p.clearRect(0, 0, W, H);
    for (let i = 0; i < 18; i++) {
      const depth = Math.pow((i + 0.5) / 18, 1.3);
      const y = lerp(hy + 3, H - 4, depth);
      const x = ((Math.sin(i * 12.9898) * 43758.5453) % 1 + 1) % 1 * W;
      const rx = lerp(10, 70, depth) * (0.6 + 0.8 * (((i * 0.618) % 1)));
      const grd = p.createRadialGradient(x, y, 0, x, y, rx);
      grd.addColorStop(0, "rgba(200,225,245,0.55)");
      grd.addColorStop(1, "rgba(200,225,245,0)");
      p.fillStyle = grd;
      p.save();
      p.translate(x, y);
      p.scale(1, 0.18);
      p.translate(-x, -y);
      p.beginPath();
      p.arc(x, y, rx, 0, Math.PI * 2);
      p.fill();
      p.restore();
    }

    // Resting puddles are barely there; lightning makes them glint (drawGround).
    g.setTransform(1, 0, 0, 1, 0, 0);
    g.globalAlpha = 0.06;
    g.drawImage(this.puddleCanvas, 0, 0);
    g.globalAlpha = 1;
  }

  //==========================================================================

  /** Called when telemetry reports a new strike. */
  strike({ key, velocity, distance, maxDistance }) {
    const { W, H } = this;
    if (W <= 0 || H <= 0) return;
    const closeness = clamp(1 - distance / Math.max(maxDistance, 1), 0, 1);
    const keyPos = clamp((key - LOWEST_KEY) / (HIGHEST_KEY - LOWEST_KEY), 0, 1);
    const x = W * (0.07 + 0.86 * keyPos) + (Math.random() - 0.5) * W * 0.03;
    const hy = this.horizonY;
    const groundY = lerp(hy + 2, H - 6, Math.pow(closeness, 1.3));
    const topY = lerp(hy - H * 0.28, -H * 0.05, closeness);

    // Return strokes: a main flash and up to three re-strikes down the same channel.
    const strokes = [{ t: 0, a: 1 }];
    let tt = 0;
    const extra = (Math.random() < 0.7 ? 1 : 0) + (Math.random() < 0.35 + 0.3 * closeness ? 1 : 0);
    for (let i = 0; i < extra; i++) {
      tt += 0.05 + Math.random() * 0.09;
      strokes.push({ t: tt, a: 0.45 + Math.random() * 0.5 });
    }

    const visible = smoothstep(0.18, 0.55, closeness);
    const bolt = visible > 0.01 ? makeBolt(x, topY, x + (Math.random() - 0.5) * W * 0.06, groundY, closeness) : null;

    const delay = distance / C_SOUND / SOUND_TIME_COMPRESSION;
    const listenerX = W / 2;
    const listenerY = H + 12;
    const reach = Math.hypot(listenerX - x, (listenerY - groundY) / 0.32);

    this.strikes.push({
      t0: this.t,
      x,
      groundY,
      topY,
      closeness,
      velocity,
      distance,
      strokes,
      bolt,
      visible,
      delay,
      reach,
      rings: 2 + Math.round(3 * (1 - closeness)),
      life: Math.max(2.5, delay + 2.2 + 1.8 * (1 - closeness)),
    });
    if (this.strikes.length > 8) this.strikes.shift();
  }

  /** Flash envelope of a strike at age `age` (s). */
  envelope(s, age, decay) {
    if (this.reducedMotion) {
      // No flicker: a slow, gentle swell.
      return age < 0 ? 0 : smoothstep(0, 0.5, age) * Math.exp(-age / 1.5) * 0.35;
    }
    let e = 0;
    for (const st of s.strokes) {
      const a = age - st.t;
      if (a >= 0) e += st.a * Math.exp(-a / decay);
    }
    return Math.min(1, e);
  }

  //==========================================================================

  step(dt, tele, params) {
    const { W, H } = this;
    if (W <= 0 || H <= 0) return;
    this.t += dt;

    // Ease telemetry toward its 30 Hz samples (jump straight to the first one).
    if (!this.primed && tele.received) {
      this.primed = true;
      this.U = Math.max(0, tele.windSpeed);
      this.gust = tele.windGust;
      this.grainRate = Math.max(0, tele.grainRate);
      this.lambda = tele.lambda > 0 ? tele.lambda : this.lambda;
      this.rain = Math.max(0, tele.rainRate);
    }
    const k = 1 - Math.exp(-dt / 0.12);
    const targetU = Math.max(0, tele.windSpeed);
    this.U += (targetU - this.U) * k;
    this.gust += (tele.windGust - this.gust) * k;
    this.grainRate += (Math.max(0, tele.grainRate) - this.grainRate) * k;
    const lam = tele.lambda > 0 ? tele.lambda : marshallPalmerLambda(Math.max(tele.rainRate, 0.01));
    this.lambda += (lam - this.lambda) * k;
    this.rain += (Math.max(0, tele.rainRate) - this.rain) * k;

    this.updateField(dt, params);
    this.updateRain(dt, params);
    this.updateSplashes(dt);

    // Expire strikes.
    this.strikes = this.strikes.filter((s) => this.t - s.t0 < s.life);
    let flash = 0;
    let rumble = 0;
    for (const s of this.strikes) {
      const age = this.t - s.t0;
      flash = Math.max(flash, this.envelope(s, age, 0.05) * (0.25 + 0.75 * s.closeness));
      const after = age - s.delay;
      if (after > 0) rumble = Math.max(rumble, (0.35 + 0.65 * s.closeness) * Math.exp(-after / (0.3 + 0.12 * params.rumbleDecay)));
    }
    this.flashLevel = flash;
    this.rumbleGlow = rumble * (0.4 + 0.6 * params.rumbleMix);
  }

  updateField(dt, params) {
    const { W, H, gw, gh, field } = this;
    const L = Math.max(params.gustLength, 1);
    const U = Math.max(this.U, 0.5);
    // Taylor's frozen turbulence: the pattern advects with the mean wind (far plane: slower parallax).
    this.advect += U * this.pxPerM * 0.18 * dt;
    // Eddies of size L turn over in ~L/U.
    this.fieldTime = (this.fieldTime || 0) + dt * (0.04 + (0.45 * U) / L) * (this.reducedMotion ? 0.35 : 1);

    const feature = W * clamp(0.16 * Math.sqrt(L / 12), 0.08, 0.6);
    const slant = this.meanSlant();
    const t = this.fieldTime;
    const cell = W / gw;
    const octaves = clamp(Math.floor(Math.log2(feature / (3 * cell))) + 1, 2, 5);
    for (let j = 0; j < gh; j++) {
      const py = ((j + 0.5) / gh) * H;
      for (let i = 0; i < gw; i++) {
        const px = ((i + 0.5) / gw) * W;
        // Shear along the rain's slant so the gust sheets lean like rain curtains.
        const sx = px - py * slant * 0.6 - this.advect;
        field[j * gw + i] = fbm3(sx / feature, py / (feature * 1.5), t, octaves);
      }
    }
  }

  /** Local gust anomaly at a canvas point (bilinear lookup in the fBm grid). */
  sampleField(x, y) {
    const { gw, gh, field, W, H } = this;
    const fx = clamp((x / W) * gw - 0.5, 0, gw - 1.001);
    const fy = clamp((y / H) * gh - 0.5, 0, gh - 1.001);
    const i = fx | 0;
    const j = fy | 0;
    const u = fx - i;
    const v = fy - j;
    const o = j * gw + i;
    const a = field[o] + (field[o + 1] - field[o]) * u;
    const b = field[o + gw] + (field[o + gw + 1] - field[o + gw]) * u;
    return a + (b - a) * v;
  }

  meanSlant() {
    const dMean = D_MIN + 1 / Math.max(this.lambda, 0.3);
    return this.U / Math.max(terminalVelocity(dMean), 0.5);
  }

  updateRain(dt, params) {
    const { W, H } = this;
    const hy = this.horizonY;
    const ppm = this.pxPerM;
    const motionScale = this.reducedMotion ? 0.3 : 1;

    // Visible drop count ~ concentration, which is flux / fall speed; scale with canvas area.
    const area = (W * H) / (1100 * 340);
    const target = Math.min(this.cap, this.grainRate * 4.6 * area * (this.reducedMotion ? 0.3 : 1));

    const dMean = D_MIN + 1 / Math.max(this.lambda, 0.3);
    const vyMean = terminalVelocity(dMean) * ppm * 0.68;
    const vxMean = this.U * ppm * 0.68;

    if (!this.seeded && this.primed && target > 1) {
      this.seeded = true;
      const n = Math.round(target);
      for (let i = 0; i < n; i++) this.spawn(Math.random() * W, null, params);
    }

    // Poisson arrivals through the upwind boundary: rate = concentration x boundary flux.
    const rate = target > 0 ? ((target * (vyMean * W + vxMean * hy)) / (W * hy)) * motionScale : 0;
    if (rate > 0) {
      this.spawnWait -= dt;
      let guard = 0;
      while (this.spawnWait <= 0 && guard++ < 400) {
        const pTop = (vyMean * W) / (vyMean * W + vxMean * hy);
        if (Math.random() < pTop) this.spawn(Math.random() * W, -4, params);
        else this.spawn(-4, Math.random(), params);
        this.spawnWait += -Math.log(1 - Math.random()) / rate;
      }
    } else {
      this.spawnWait = 0;
    }

    // Advance: horizontal speed relaxes to the local gust, vertical is the terminal velocity.
    const relax = 1 - Math.exp(-dt / 0.12);
    const U = this.U;
    for (let i = 0; i < this.count; i++) {
      const z = this.pz[i];
      const local = U * Math.max(0, 1 + 1.1 * this.sampleField(this.px[i], this.py[i]));
      this.vx[i] += (local * ppm * z - this.vx[i]) * relax;
      this.px[i] += this.vx[i] * dt * motionScale;
      this.py[i] += this.vy[i] * dt * motionScale;

      if (this.py[i] >= this.pland[i] || this.px[i] > W + 30) {
        if (this.py[i] >= this.pland[i]) this.maybeSplash(i, params);
        this.kill(i);
        i--;
      }
    }
  }

  spawn(x, y, params) {
    if (this.count >= this.cap) return;
    const i = this.count++;
    const d = sampleDiameter(this.lambda);
    const z = 0.35 + 0.65 * Math.random();
    const land = lerp(this.horizonY + 1, this.H - 2, Math.pow((z - 0.35) / 0.65, 1.4));
    this.pd[i] = d;
    this.pz[i] = z;
    this.pland[i] = land;
    this.vy[i] = terminalVelocity(d) * this.pxPerM * z;
    this.vx[i] = this.U * this.pxPerM * z;
    this.px[i] = x;
    // y === null: seed anywhere above the ground; 0..1: fraction of the landing height (left edge).
    if (y === null) this.py[i] = Math.random() * land;
    else if (y >= 0 && y <= 1) this.py[i] = y * land;
    else this.py[i] = y;
  }

  kill(i) {
    const j = --this.count;
    if (i === j) return;
    this.px[i] = this.px[j];
    this.py[i] = this.py[j];
    this.vx[i] = this.vx[j];
    this.vy[i] = this.vy[j];
    this.pd[i] = this.pd[j];
    this.pz[i] = this.pz[j];
    this.pland[i] = this.pland[j];
  }

  maybeSplash(i, params) {
    if (this.splashes.length > (this.reducedMotion ? 40 : 160)) return;
    const d = this.pd[i];
    const p = (0.12 + 0.6 * params.surface) * (d > 0.9 ? 1 : 0.25);
    if (Math.random() > p) return;
    this.splashes.push({
      x: this.px[i],
      y: this.pland[i],
      age: 0,
      life: 0.22 + 0.55 * params.surface + 0.05 * d,
      size: (1.2 + d * 1.5) * this.pz[i],
      z: this.pz[i],
    });
  }

  updateSplashes(dt) {
    for (const s of this.splashes) s.age += dt;
    if (this.splashes.length) this.splashes = this.splashes.filter((s) => s.age < s.life);
  }

  //==========================================================================

  draw(ctx) {
    const { W, H, dpr } = this;
    if (W <= 0 || H <= 0) return;
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.globalCompositeOperation = "source-over";
    ctx.globalAlpha = 1;
    ctx.clearRect(0, 0, ctx.canvas.width, ctx.canvas.height);
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

    this.drawMist();
    this.drawHorizonGlows(ctx);
    this.drawRain(ctx);
    this.drawBolts(ctx);
    this.drawGround(ctx);
    this.drawRings(ctx);
    this.drawSkyFlash(ctx);
  }

  /** Writes the gust field into the low-resolution mist canvas (stretched by CSS). */
  drawMist() {
    const { gw, gh, field, mistImage, H } = this;
    if (!mistImage) return;
    const data = mistImage.data;
    const flash = this.flashLevel;
    const turbulence = clamp(this.gustAmount ?? 0.4, 0, 1);
    // Gust sheets need wind to exist and rain to be seen.
    const windy = smoothstep(0.5, 7, this.U);
    const sheetAmt = clamp((0.2 + 0.5 * turbulence) * windy + this.rain / 90, 0, 1);
    const hyFrac = this.horizonY / H;

    for (let j = 0; j < gh; j++) {
      const yy = (j + 0.5) / gh;
      const cloudBase = smoothstep(0.5, 0.0, yy);
      const aboveGround = smoothstep(hyFrac + 0.04, hyFrac - 0.08, yy);
      for (let i = 0; i < gw; i++) {
        const o = j * gw + i;
        const w = field[o];
        const sheet = smoothstep(0.06, 0.42, w) * sheetAmt * aboveGround;
        const cloud = cloudBase * (0.45 + 0.55 * smoothstep(-0.35, 0.35, w));

        // Mist is pale and cool; clouds are ink-blue; lightning lights both from within.
        const lit = flash * (0.55 + 0.45 * cloudBase);
        const mix = sheet / (sheet + cloud + 1e-3);
        const r = lerp(lerp(28, 120, mix), 215, lit);
        const g = lerp(lerp(42, 158, mix), 222, lit);
        const b = lerp(lerp(64, 182, mix), 245, lit);
        const a = clamp(sheet * 0.34 + cloud * (0.55 + 0.6 * lit), 0, 0.95);
        const p = o * 4;
        data[p] = r;
        data[p + 1] = g;
        data[p + 2] = b;
        data[p + 3] = a * 255;
      }
    }
    this.mistCtx.putImageData(mistImage, 0, 0);
  }

  drawRain(ctx) {
    const n = this.count;
    if (!n) return;
    const { px, py, vx, vy, pd, pz } = this;
    const flash = this.flashLevel;
    ctx.lineCap = "round";
    ctx.globalCompositeOperation = "lighter";

    // Bucket by drop size so each bucket is one path and one stroke.
    const edges = [0.5, 0.9, 1.5, 2.3, 3.4, Infinity];
    for (let b = 0; b < STREAK_BUCKETS; b++) {
      const lo = b === 0 ? 0 : edges[b - 1];
      const hi = edges[b];
      const dRep = b === 0 ? 0.35 : Math.min((lo + Math.min(hi, D_MAX)) / 2, D_MAX);
      ctx.beginPath();
      let any = false;
      for (let i = 0; i < n; i++) {
        const d = pd[i];
        if (d < lo || d >= hi) continue;
        any = true;
        const z = pz[i];
        const e = EXPOSURE * (0.8 + 0.4 * z);
        ctx.moveTo(px[i], py[i]);
        ctx.lineTo(px[i] - vx[i] * e, py[i] - vy[i] * e);
      }
      if (!any) continue;
      ctx.lineWidth = 0.55 + dRep * 0.42;
      const alpha = clamp(0.15 + dRep * 0.08, 0.12, 0.46) * (1 + 1.4 * flash);
      ctx.strokeStyle = `rgba(${Math.round(lerp(150, 235, flash))},${Math.round(lerp(205, 240, flash))},${Math.round(lerp(232, 255, flash))},${alpha.toFixed(3)})`;
      ctx.stroke();
    }
    ctx.globalCompositeOperation = "source-over";
  }

  drawGround(ctx) {
    const { W, H } = this;
    const hy = this.horizonY;
    const flash = this.flashLevel;

    // Puddles glint under lightning.
    if (flash > 0.02) {
      ctx.save();
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.globalAlpha = Math.min(1, 0.9 * flash);
      ctx.globalCompositeOperation = "lighter";
      ctx.drawImage(this.puddleCanvas, 0, 0);
      ctx.restore();
      ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
    }

    // Splash crowns / puddle rings.
    if (this.splashes.length) {
      ctx.globalCompositeOperation = "lighter";
      ctx.lineWidth = 0.8;
      for (let bucket = 0; bucket < 3; bucket++) {
        ctx.beginPath();
        let any = false;
        for (const s of this.splashes) {
          const f = s.age / s.life;
          if (Math.min(2, Math.floor(f * 3)) !== bucket) continue;
          any = true;
          const rx = s.size * (0.4 + 2.4 * f);
          ctx.moveTo(s.x + rx, s.y);
          ctx.ellipse(s.x, s.y, rx, rx * 0.3, 0, 0, Math.PI * 2);
        }
        if (!any) continue;
        const a = [0.38, 0.22, 0.09][bucket] * (1 + flash);
        ctx.strokeStyle = `rgba(170,215,235,${a.toFixed(3)})`;
        ctx.stroke();
      }
      ctx.globalCompositeOperation = "source-over";
    }

    // Wet-ground reflection of the sky flash.
    if (flash > 0.01) {
      const grd = ctx.createLinearGradient(0, hy, 0, H);
      grd.addColorStop(0, `rgba(200,215,245,${(0.18 * flash).toFixed(3)})`);
      grd.addColorStop(1, "rgba(200,215,245,0)");
      ctx.globalCompositeOperation = "lighter";
      ctx.fillStyle = grd;
      ctx.fillRect(0, hy, W, H - hy);
      ctx.globalCompositeOperation = "source-over";
    }

    // Thunder arriving: the ground hums warm along the bottom edge.
    if (this.rumbleGlow > 0.01) {
      const grd = ctx.createLinearGradient(0, H, 0, H - H * 0.22);
      grd.addColorStop(0, `rgba(212,163,115,${(0.22 * this.rumbleGlow).toFixed(3)})`);
      grd.addColorStop(1, "rgba(212,163,115,0)");
      ctx.fillStyle = grd;
      ctx.fillRect(0, H - H * 0.22, W, H * 0.22);
    }
  }

  drawHorizonGlows(ctx) {
    const { W, H } = this;
    const hy = this.horizonY;
    ctx.globalCompositeOperation = "lighter";
    for (const s of this.strikes) {
      const age = this.t - s.t0;
      const e = this.envelope(s, age, lerp(0.2, 0.07, s.closeness));
      if (e < 0.005) continue;
      // Distant strikes light the horizon from behind the ridge; near ones glow higher up.
      const cy = lerp(hy - H * 0.03, hy - H * 0.35, s.closeness);
      const r = W * lerp(0.16, 0.42, s.closeness);
      const a = e * lerp(0.42, 0.4, s.closeness);
      const grd = ctx.createRadialGradient(s.x, cy, 0, s.x, cy, r);
      grd.addColorStop(0, `rgba(251,241,181,${a.toFixed(3)})`);
      grd.addColorStop(0.4, `rgba(190,200,240,${(a * 0.35).toFixed(3)})`);
      grd.addColorStop(1, "rgba(150,170,230,0)");
      ctx.fillStyle = grd;
      ctx.save();
      ctx.translate(s.x, cy);
      ctx.scale(1, 0.55);
      ctx.translate(-s.x, -cy);
      ctx.fillRect(s.x - r, cy - r, 2 * r, 2 * r);
      ctx.restore();
    }
    ctx.globalCompositeOperation = "source-over";
  }

  drawBolts(ctx) {
    if (this.reducedMotion) return;
    ctx.globalCompositeOperation = "lighter";
    ctx.lineJoin = "round";
    ctx.lineCap = "round";
    for (const s of this.strikes) {
      if (!s.bolt) continue;
      const age = this.t - s.t0;
      const e = this.envelope(s, age, 0.085);
      const a = e * s.visible;
      if (a < 0.01) continue;
      const width = lerp(0.8, 2.4, s.closeness);
      for (const branch of s.bolt) {
        const ba = a * branch.alpha;
        const pts = branch.points;
        ctx.beginPath();
        ctx.moveTo(pts[0], pts[1]);
        for (let i = 2; i < pts.length; i += 2) ctx.lineTo(pts[i], pts[i + 1]);
        ctx.strokeStyle = `rgba(243,225,136,${(0.10 * ba).toFixed(3)})`;
        ctx.lineWidth = width * branch.width * 7;
        ctx.stroke();
        ctx.strokeStyle = `rgba(251,241,181,${(0.32 * ba).toFixed(3)})`;
        ctx.lineWidth = width * branch.width * 2.6;
        ctx.stroke();
        ctx.strokeStyle = `rgba(255,253,240,${(0.95 * ba).toFixed(3)})`;
        ctx.lineWidth = width * branch.width;
        ctx.stroke();
      }
    }
    ctx.globalCompositeOperation = "source-over";
  }

  drawRings(ctx) {
    const { H } = this;
    ctx.lineWidth = 1;
    for (const s of this.strikes) {
      const age = this.t - s.t0;
      if (age < 0) continue;
      const speed = s.reach / Math.max(s.delay, 0.05); // px/s on the ground plane
      const fadeIn = this.reducedMotion ? 1 : smoothstep(0, 0.06, age);

      // The wavefront in transit (thin) and, behind it, multipath echoes (the rolling rumble).
      for (let k = 0; k < s.rings; k++) {
        const lag = k * lerp(0.14, 0.32, 1 - s.closeness);
        const a0 = age - lag;
        if (a0 <= 0) continue;
        const rx = speed * a0;
        const fade = Math.exp(-a0 / (0.7 + 1.6 * (1 - s.closeness) + s.delay * 0.6));
        const alpha = fadeIn * fade * (k === 0 ? 0.42 : 0.22 / k) * (0.55 + 0.45 * s.closeness);
        if (alpha < 0.01 || rx > s.reach * 2.4) continue;
        const arrived = a0 >= s.delay;
        ctx.strokeStyle = arrived
          ? `rgba(212,163,115,${clamp(alpha, 0, 1).toFixed(3)})`
          : `rgba(243,225,136,${clamp(alpha * 0.8, 0, 1).toFixed(3)})`;
        ctx.lineWidth = arrived ? 1.4 : 1;
        ctx.beginPath();
        ctx.ellipse(s.x, s.groundY, rx, rx * 0.32, 0, Math.PI, Math.PI * 2);
        ctx.stroke();
        // Lower half drawn fainter (ground in front of the strike).
        ctx.globalAlpha = 0.45;
        ctx.beginPath();
        ctx.ellipse(s.x, s.groundY, rx, Math.min(rx * 0.32, H), 0, 0, Math.PI);
        ctx.stroke();
        ctx.globalAlpha = 1;
      }

      // Ground point marker.
      const mark = Math.exp(-age / 1.2) * (0.3 + 0.7 * s.closeness);
      if (mark > 0.02) {
        ctx.fillStyle = `rgba(251,241,181,${(0.8 * mark).toFixed(3)})`;
        ctx.beginPath();
        ctx.ellipse(s.x, s.groundY, 2.2 + 3 * s.closeness, 0.9 + 1.2 * s.closeness, 0, 0, Math.PI * 2);
        ctx.fill();
      }
    }
  }

  drawSkyFlash(ctx) {
    if (this.reducedMotion) return;
    let a = 0;
    for (const s of this.strikes) {
      const e = this.envelope(s, this.t - s.t0, 0.04);
      a = Math.max(a, e * 0.42 * Math.pow(s.closeness, 1.6));
    }
    if (a < 0.004) return;
    ctx.globalCompositeOperation = "lighter";
    ctx.fillStyle = `rgba(205,215,250,${a.toFixed(3)})`;
    ctx.fillRect(0, 0, this.W, this.H);
    ctx.globalCompositeOperation = "source-over";
  }
}

//==============================================================================
// Lightning channel: midpoint displacement with a few side branches.

function displace(x0, y0, x1, y1, roughness, iterations) {
  let pts = [x0, y0, x1, y1];
  let amp = Math.hypot(x1 - x0, y1 - y0) * roughness;
  for (let it = 0; it < iterations; it++) {
    const next = [pts[0], pts[1]];
    for (let i = 0; i < pts.length - 2; i += 2) {
      const ax = pts[i];
      const ay = pts[i + 1];
      const bx = pts[i + 2];
      const by = pts[i + 3];
      const len = Math.hypot(bx - ax, by - ay) || 1;
      const nx = -(by - ay) / len;
      const ny = (bx - ax) / len;
      const off = (Math.random() - 0.5) * 2 * amp;
      next.push((ax + bx) / 2 + nx * off, (ay + by) / 2 + ny * off, bx, by);
    }
    pts = next;
    amp *= 0.52;
  }
  return pts;
}

function makeBolt(x0, y0, x1, y1, closeness) {
  const main = displace(x0, y0, x1, y1, 0.16, 7);
  const branches = [{ points: main, alpha: 1, width: 1 }];
  const length = Math.hypot(x1 - x0, y1 - y0);
  const count = 2 + Math.floor(Math.random() * (2 + 3 * closeness));
  const segs = main.length / 2;
  for (let b = 0; b < count; b++) {
    const idx = Math.floor(segs * (0.1 + 0.6 * Math.random()));
    const sx = main[idx * 2];
    const sy = main[idx * 2 + 1];
    const side = Math.random() < 0.5 ? -1 : 1;
    const ang = Math.PI / 2 + side * (0.35 + Math.random() * 0.6);
    const len = length * (0.12 + Math.random() * 0.22);
    const ex = sx + Math.cos(ang) * len;
    const ey = sy + Math.sin(ang) * len;
    branches.push({ points: displace(sx, sy, ex, ey, 0.22, 5), alpha: 0.35 + Math.random() * 0.3, width: 0.55 });
  }
  return branches;
}
