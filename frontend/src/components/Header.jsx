import React, { useRef } from "react";
import { bridgeMode, missingRelays, useTelemetry } from "../juce";

const HISTORY = 90; // samples (~9 s at 10 Hz)

function Sparkline({ values, color, min = 0 }) {
  if (values.length < 2) return <svg className="h-[0.75rem] w-full" aria-hidden="true" />;
  let lo = min === null ? Infinity : min;
  let hi = -Infinity;
  for (const v of values) {
    if (min === null) lo = Math.min(lo, v);
    hi = Math.max(hi, v);
  }
  if (!(hi > lo)) hi = lo + 1;
  const n = values.length;
  const pts = values
    .map((v, i) => `${((i + HISTORY - n) / (HISTORY - 1)) * 100},${(11 - ((v - lo) / (hi - lo)) * 10).toFixed(2)}`)
    .join(" ");
  return (
    <svg className="h-[0.75rem] w-full" viewBox="0 0 100 12" preserveAspectRatio="none" aria-hidden="true">
      <polyline points={pts} fill="none" stroke={color} strokeWidth="1.2" vectorEffect="non-scaling-stroke" strokeLinejoin="round" opacity="0.75" />
    </svg>
  );
}

function GustBar({ g }) {
  const x = Math.max(-3, Math.min(3, g)) / 3; // -1..1
  return (
    <svg className="h-[0.75rem] w-full" viewBox="0 0 100 12" preserveAspectRatio="none" aria-hidden="true">
      <line x1="0" y1="6" x2="100" y2="6" stroke="rgba(122,142,166,0.25)" strokeWidth="1" vectorEffect="non-scaling-stroke" />
      <line x1="50" y1="2" x2="50" y2="10" stroke="rgba(122,142,166,0.45)" strokeWidth="1" vectorEffect="non-scaling-stroke" />
      <rect x={Math.min(50, 50 + x * 50)} y="4" width={Math.abs(x * 50)} height="4" fill={x >= 0 ? "#c5d4e2" : "#5c6f88"} rx="1" />
    </svg>
  );
}

function Readout({ label, symbol, value, unit, children, testid, title }) {
  return (
    <div className="flex min-w-0 flex-col gap-[0.15rem] px-[0.65rem]" title={title}>
      <div className="eyebrow flex items-baseline gap-1 truncate">
        {symbol ? <span className="font-display text-[0.8rem] normal-case italic tracking-normal text-mist-300">{symbol}</span> : null}
        <span>{label}</span>
      </div>
      <div className="flex items-baseline gap-1 whitespace-nowrap">
        <span className="num text-[1.02rem] leading-none text-mist-100" data-readout={testid}>
          {value}
        </span>
        {unit ? <span className="text-[0.62rem] text-mist-500">{unit}</span> : null}
      </div>
      {children}
    </div>
  );
}

function Wordmark() {
  return (
    <div className="flex min-w-0 items-center gap-[0.7rem]">
      <svg viewBox="0 0 32 40" className="h-[2.35rem] w-auto shrink-0" aria-hidden="true">
        <defs>
          <linearGradient id="mark-drop" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stopColor="#a8e6f4" stopOpacity="0.95" />
            <stop offset="100%" stopColor="#2f97b5" stopOpacity="0.55" />
          </linearGradient>
        </defs>
        <path d="M16 2.5C16 2.5 4 16.5 4 25.5a12 12 0 0 0 24 0C28 16.5 16 2.5 16 2.5Z" fill="rgba(79,188,216,0.08)" stroke="url(#mark-drop)" strokeWidth="1.6" />
        <path d="M18.2 14.5 12.4 25h4.4l-2.2 9.2 7.2-11.6h-4.6l1-8.1Z" fill="#f3e188" />
      </svg>
      <div className="min-w-0">
        <h1 className="flex items-baseline gap-[0.55rem] leading-none">
          <span className="font-display text-[1.95rem] font-medium italic text-mist-100">Petrichor</span>
          <span className="font-display text-[0.95rem] font-semibold uppercase tracking-[0.34em] text-earth-400">Piano</span>
        </h1>
        <p className="mt-[0.3rem] flex items-center gap-2 truncate text-[0.66rem] text-mist-400">
          <span className="truncate">Lightning hammers &middot; Kolmogorov wind &middot; Marshall&#8211;Palmer rain</span>
          {bridgeMode === "mock" ? (
            <span className="shrink-0 rounded-full px-2 py-[1px] text-[0.58rem] font-bold uppercase tracking-[0.14em] text-earth-300 ring-1 ring-earth-500/40">
              Preview &middot; simulated storm
            </span>
          ) : null}
          {missingRelays.length ? (
            <span className="shrink-0 rounded-full px-2 py-[1px] text-[0.58rem] font-bold uppercase tracking-[0.14em] text-bolt-300 ring-1 ring-bolt-500/40" title={missingRelays.join(", ")}>
              {missingRelays.length} control{missingRelays.length > 1 ? "s" : ""} offline
            </span>
          ) : null}
        </p>
      </div>
    </div>
  );
}

export default function Header() {
  const t = useTelemetry(10);
  const hist = useRef({ last: 0, U: [], R: [], drops: [] });
  const h = hist.current;
  if (t.time !== h.last) {
    h.last = t.time;
    const push = (arr, v) => {
      arr.push(v);
      if (arr.length > HISTORY) arr.shift();
    };
    push(h.U, t.windSpeed);
    push(h.R, t.rainRate);
    push(h.drops, t.grainRate);
  }

  return (
    <header className="flex shrink-0 items-center justify-between gap-[1.2rem]">
      <Wordmark />
      <div className="flex shrink-0 items-stretch divide-x divide-mist-600/30" aria-label="Storm telemetry">
        <Readout label="Wind" symbol="U" value={t.windSpeed.toFixed(1)} unit="m/s" testid="windSpeed" title="Instantaneous wind speed U(t) = U (1 + I G(t))">
          <Sparkline values={h.U} color="#c5d4e2" />
        </Readout>
        <Readout label="Gust" symbol="G" value={`×${t.gustFactor.toFixed(2)}`} testid="gustFactor" title="Gust factor 1 + I G(t); bar shows the unit-variance gust G(t)">
          <GustBar g={t.windGust} />
        </Readout>
        <Readout label="Rain" symbol="R" value={t.rainRate.toFixed(1)} unit="mm/h" testid="rainRate" title="Rainfall rate R(t), raised by gusts">
          <Sparkline values={h.R} color="#7dd3e8" />
        </Readout>
        <Readout label="Slope" symbol={"Λ"} value={t.lambda.toFixed(2)} unit="/mm" testid="lambda" title="Marshall-Palmer slope Lambda = 4.1 R^-0.21 (smaller = bigger drops)">
          <div className="text-[0.6rem] leading-[0.75rem] text-mist-500">
            D&#772; <span className="num text-mist-300">{t.meanDropMM.toFixed(2)}</span> mm
          </div>
        </Readout>
        <Readout label="Drops" value={Math.round(t.grainRate).toString()} unit="/s" testid="grainRate" title="Drop impacts per second on the collector">
          <Sparkline values={h.drops} color="#4fbcd8" />
        </Readout>
        <Readout label="Voices" value={String(Math.round(t.activeVoices))} testid="activeVoices" title="Sounding piano voices">
          <div className="text-[0.6rem] leading-[0.75rem] text-mist-500">
            strikes <span className="num text-bolt-300">{Math.round(t.strikeCount)}</span>
          </div>
        </Readout>
      </div>
    </header>
  );
}
