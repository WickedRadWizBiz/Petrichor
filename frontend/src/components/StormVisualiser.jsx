import React, { useEffect, useRef, useState } from "react";
import { getParamValue, getTelemetry, subscribeTelemetry, useParam, useTelemetry } from "../juce";
import { StormScene, SOUND_TIME_COMPRESSION } from "../storm/scene";
import { N0, terminalVelocity, marshallPalmerLambda } from "../storm/atmos";
import { SPEED_OF_SOUND, clamp, formatDistance, noteName } from "../params";

function readParams() {
  return {
    stormDistance: getParamValue("storm_distance"),
    surface: getParamValue("rain_surface"),
    gustLength: getParamValue("gust_length"),
    rumbleDecay: getParamValue("rumble_decay"),
    rumbleMix: getParamValue("rumble_mix"),
    turbulence: getParamValue("turbulence"),
  };
}

function prefersReducedMotion() {
  try {
    return window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  } catch {
    return false;
  }
}

//==============================================================================
// Overlays

function BoltIcon({ className }) {
  return (
    <svg viewBox="0 0 16 16" className={className} aria-hidden="true">
      <path d="M9.6 1 3.8 9h3.6L6.2 15l6-8.2H8.6L9.6 1Z" fill="#f3e188" />
    </svg>
  );
}

function StrikeCard({ strike }) {
  if (!strike) {
    return (
      <div className="overlay-card text-mist-400">
        <div className="eyebrow mb-1 flex items-center gap-1.5">
          <BoltIcon className="h-3 w-3 opacity-60" /> Waiting for a strike
        </div>
        <div className="text-[0.66rem] leading-snug">Play the keys below. Velocity sets the distance.</div>
      </div>
    );
  }
  const delay = strike.distance / SPEED_OF_SOUND;
  const closeness = clamp(1 - strike.distance / Math.max(strike.maxDistance, 1), 0, 1);
  return (
    <div className="overlay-card" key={strike.n} data-testid="strike-card">
      <div className="eyebrow mb-1 flex items-center gap-1.5 !text-bolt-400">
        <BoltIcon className="h-3 w-3" /> Strike {strike.n}
      </div>
      <div className="flex items-baseline gap-2 whitespace-nowrap">
        <span className="font-display text-[1.25rem] font-semibold leading-none text-mist-100">{noteName(strike.key)}</span>
        <span className="num text-[0.7rem] text-mist-300">V {Math.round(strike.velocity)}</span>
      </div>
      <div className="num mt-1 whitespace-nowrap text-[0.66rem] text-mist-300">
        {formatDistance(strike.distance)} &middot; thunder +{delay.toFixed(1)} s
      </div>
      <div className="mt-1.5 h-[3px] w-full overflow-hidden rounded-full bg-mist-600/30">
        <div className="h-full rounded-full bg-bolt-400" style={{ width: `${Math.max(3, closeness * 100)}%` }} />
      </div>
      <div className="mt-0.5 flex justify-between text-[0.55rem] uppercase tracking-[0.12em] text-mist-500">
        <span>far</span>
        <span>overhead</span>
      </div>
    </div>
  );
}

function WindVane({ t }) {
  const dMean = t.meanDropMM > 0 ? t.meanDropMM : 1;
  const vT = terminalVelocity(dMean);
  const theta = Math.atan2(Math.max(0, t.windSpeed), vT); // slant from vertical
  const deg = (theta * 180) / Math.PI;
  const len = 26;
  const x2 = 6 + Math.sin(theta) * len;
  const y2 = 4 + Math.cos(theta) * len;
  return (
    <div className="overlay-card flex items-center gap-2.5" title="Drops fall along the vector sum of terminal velocity and wind">
      <svg viewBox="0 0 40 34" className="h-[2.1rem] w-auto" aria-hidden="true">
        <line x1="6" y1="4" x2="6" y2="30" stroke="rgba(122,142,166,0.35)" strokeDasharray="2 3" />
        <line x1="6" y1="4" x2={x2} y2={y2} stroke="#a8e6f4" strokeWidth="1.8" strokeLinecap="round" />
        <circle cx={x2} cy={y2} r="2.2" fill="#a8e6f4" />
      </svg>
      <div>
        <div className="eyebrow">Rain slant</div>
        <div className="num text-[0.85rem] leading-tight text-mist-100">{deg.toFixed(0)}&deg;</div>
        <div className="num whitespace-nowrap text-[0.6rem] text-mist-400">
          v<sub>T</sub> {vT.toFixed(1)} &middot; U {t.windSpeed.toFixed(1)} m/s
        </div>
      </div>
    </div>
  );
}

/** Marshall-Palmer N(D) on a log axis: the live curve against the no-gust baseline. */
function DropSpectrum({ t, baseRate }) {
  const W = 150;
  const H = 62;
  const Dmax = 6;
  const yMin = -2.5; // log10 N
  const yMax = 4.2;
  const lam = t.lambda > 0 ? t.lambda : marshallPalmerLambda(baseRate);
  const lamBase = marshallPalmerLambda(baseRate);
  const toX = (d) => (d / Dmax) * W;
  const toY = (logN) => H - ((clamp(logN, yMin, yMax) - yMin) / (yMax - yMin)) * H;
  const curve = (l) => {
    const pts = [];
    for (let i = 0; i <= 30; i++) {
      const d = 0.1 + (i / 30) * (Dmax - 0.1);
      pts.push(`${toX(d).toFixed(1)},${toY(Math.log10(N0) - l * d * Math.LOG10E).toFixed(1)}`);
    }
    return pts;
  };
  const live = curve(lam);
  const area = `M ${live[0]} L ${live.join(" L ")} L ${W},${H} L ${toX(0.1).toFixed(1)},${H} Z`;
  return (
    <div className="overlay-card" title="Drop size distribution N(D) = N0 exp(-Lambda D); dashed: without gusts">
      <div className="mb-1 flex items-baseline justify-between gap-3 pt-[0.15rem]">
        <span className="eyebrow">Drop sizes</span>
        <span className="whitespace-nowrap font-display text-[0.8rem] italic leading-none text-mist-300">
          N(D) = N<sub className="align-[-0.15em] text-[0.62em] leading-none">0</sub>e
          <sup className="align-[0.34em] text-[0.62em] leading-none">&minus;&Lambda;D</sup>
        </span>
      </div>
      <svg viewBox={`0 0 ${W} ${H + 10}`} className="block h-[3.6rem] w-[8.6rem]" aria-hidden="true">
        {[0, 2, 4, 6].map((d) => (
          <g key={d}>
            <line x1={toX(d)} y1="0" x2={toX(d)} y2={H} stroke="rgba(122,142,166,0.12)" />
            <text x={Math.min(toX(d) + 1, W - 12)} y={H + 9} fontSize="7.5" fill="#5c6f88" fontFamily="IBM Plex Mono, monospace">
              {d}
              {d === 6 ? "mm" : ""}
            </text>
          </g>
        ))}
        <path d={area} fill="rgba(79,188,216,0.14)" />
        <polyline points={curve(lamBase).join(" ")} fill="none" stroke="rgba(197,212,226,0.4)" strokeDasharray="3 3" strokeWidth="1" />
        <polyline points={live.join(" ")} fill="none" stroke="#7dd3e8" strokeWidth="1.6" />
      </svg>
      <div className="num mt-0.5 flex justify-between gap-3 whitespace-nowrap text-[0.6rem] text-mist-400">
        <span>
          &Lambda; <span className="text-mist-200">{lam.toFixed(2)}</span>/mm
        </span>
        <span>
          impact <span className="text-mist-200">{t.impactSpeed.toFixed(1)}</span> m/s
        </span>
      </div>
    </div>
  );
}

function Overlays({ strike }) {
  const t = useTelemetry(8);
  const rain = useParam("rain_rate");
  return (
    <>
      <div className="pointer-events-none absolute left-[0.7rem] top-[0.7rem]">
        <StrikeCard strike={strike} />
      </div>
      <div className="pointer-events-none absolute right-[0.7rem] top-[0.7rem]">
        <WindVane t={t} />
      </div>
      <div className="pointer-events-none absolute bottom-[0.7rem] left-[0.7rem]">
        <DropSpectrum t={t} baseRate={rain.value} />
      </div>
      <div className="pointer-events-none absolute bottom-[0.8rem] right-[0.8rem] max-w-[46%] text-right text-[0.6rem] leading-snug text-mist-500">
        <span className="text-bolt-400/80">&#9675;</span> thunder wavefront, sound shown {SOUND_TIME_COMPRESSION}&times; faster
        <br />
        gust sheets: fBm, H = 1/3 (Kolmogorov)
      </div>
    </>
  );
}

//==============================================================================

export default function StormVisualiser() {
  const wrapRef = useRef(null);
  const canvasRef = useRef(null);
  const bgRef = useRef(null);
  const mistRef = useRef(null);
  const [strike, setStrike] = useState(null);

  useEffect(() => {
    const wrap = wrapRef.current;
    const canvas = canvasRef.current;
    const ctx = canvas.getContext("2d");
    if (!ctx) return undefined;

    const scene = new StormScene({
      bgCanvas: bgRef.current,
      mistCanvas: mistRef.current,
      reducedMotion: prefersReducedMotion(),
    });
    let tele = getTelemetry();
    let lastCount = null;
    let raf = 0;
    let running = false;
    let last = performance.now();
    let strikeN = 0;
    let frames = 0;

    const unsubscribe = subscribeTelemetry((t) => {
      tele = t;
      if (!t.received) return;
      if (lastCount === null || t.strikeCount < lastCount) {
        lastCount = t.strikeCount;
        return;
      }
      if (t.strikeCount > lastCount) {
        lastCount = t.strikeCount;
        const info = {
          key: t.lastStrikeKey,
          velocity: t.lastStrikeVelocity,
          distance: t.lastStrikeDistance,
          maxDistance: getParamValue("storm_distance"),
        };
        if (!document.hidden) scene.strike(info);
        setStrike({ ...info, n: ++strikeN });
      }
    });

    const resize = () => {
      const r = wrap.getBoundingClientRect();
      const w = Math.max(1, Math.floor(r.width));
      const h = Math.max(1, Math.floor(r.height));
      const dpr = Math.min(window.devicePixelRatio || 1, 2);
      canvas.width = Math.round(w * dpr);
      canvas.height = Math.round(h * dpr);
      for (const c of [canvas, bgRef.current]) {
        c.style.width = `${w}px`;
        c.style.height = `${h}px`;
      }
      scene.resize(w, h, dpr);
      if (!running) {
        scene.step(0, tele, readParams());
        scene.draw(ctx);
      }
    };
    const ro = new ResizeObserver(resize);
    ro.observe(wrap);
    resize();

    const frame = (now) => {
      const dt = Math.min(0.05, Math.max(0, (now - last) / 1000));
      last = now;
      const params = readParams();
      scene.gustAmount = params.turbulence;
      scene.step(dt, tele, params);
      scene.draw(ctx);
      if ((++frames & 31) === 0) wrap.dataset.particles = String(scene.count); // for diagnostics
      raf = requestAnimationFrame(frame);
    };
    const start = () => {
      if (running) return;
      running = true;
      last = performance.now();
      raf = requestAnimationFrame(frame);
    };
    const stop = () => {
      running = false;
      cancelAnimationFrame(raf);
    };
    const onVisibility = () => (document.hidden ? stop() : start());
    document.addEventListener("visibilitychange", onVisibility);
    onVisibility();

    let mq = null;
    const onMotionPref = () => {
      scene.reducedMotion = mq.matches;
      scene.cap = mq.matches ? 450 : 1500;
    };
    try {
      mq = window.matchMedia("(prefers-reduced-motion: reduce)");
      mq.addEventListener("change", onMotionPref);
    } catch {
      mq = null;
    }

    return () => {
      stop();
      unsubscribe();
      ro.disconnect();
      document.removeEventListener("visibilitychange", onVisibility);
      if (mq) mq.removeEventListener("change", onMotionPref);
    };
  }, []);

  return (
    <div
      ref={wrapRef}
      className="relative h-full w-full overflow-hidden rounded-[16px] bg-ink-950"
      style={{ boxShadow: "inset 0 0 0 1px rgba(122,142,166,0.14), 0 18px 40px -24px rgba(0,0,0,0.9)" }}
      role="img"
      aria-label="Storm visualiser: rain, gusts and lightning driven by the audio engine"
    >
      <canvas ref={bgRef} className="absolute inset-0 block" aria-hidden="true" />
      <canvas ref={mistRef} className="absolute inset-0 block h-full w-full" aria-hidden="true" />
      <canvas ref={canvasRef} className="absolute inset-0 block" aria-hidden="true" />
      <div
        className="pointer-events-none absolute inset-0"
        style={{ background: "radial-gradient(120% 95% at 50% 55%, rgba(3,5,10,0) 45%, rgba(3,5,10,0.55) 100%)" }}
      />
      <Overlays strike={strike} />
    </div>
  );
}
