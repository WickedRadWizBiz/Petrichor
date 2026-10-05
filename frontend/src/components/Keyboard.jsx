import React, { useEffect, useMemo, useRef } from "react";
import { getParamValue, getTelemetry, playNote, releaseNote, subscribeTelemetry } from "../juce";
import {
  HIGHEST_KEY,
  LOWEST_KEY,
  SPEED_OF_SOUND,
  clamp,
  formatDistance,
  isBlackKey,
  noteName,
  velocityToDistance,
} from "../params";

const BLACK_HEIGHT = 0.62;
const BLACK_WIDTH = 0.62; // of a white key

function buildLayout() {
  const whites = [];
  const blacks = [];
  for (let m = LOWEST_KEY; m <= HIGHEST_KEY; m++) {
    if (isBlackKey(m)) blacks.push({ midi: m, boundary: whites.length });
    else whites.push({ midi: m, index: whites.length });
  }
  const blackAtBoundary = new Map(blacks.map((b) => [b.boundary, b.midi]));
  return { whites, blacks, blackAtBoundary };
}

export default function Keyboard() {
  const layout = useMemo(buildLayout, []);
  const bedRef = useRef(null);
  const keyEls = useRef(new Map());
  const pointers = useRef(new Map()); // pointerId -> midi
  const downCount = useRef(new Map()); // midi -> number of pointers holding it
  const readoutRef = useRef(null);

  const nWhite = layout.whites.length;

  // Pointer position -> key and velocity (bottom of the key = 127 = a close, loud strike).
  const hitTest = (clientX, clientY) => {
    const bed = bedRef.current;
    if (!bed) return null;
    const r = bed.getBoundingClientRect();
    const x = clientX - r.left;
    const y = clientY - r.top;
    if (x < 0 || x >= r.width || y < 0 || y >= r.height) return null;
    const ww = r.width / nWhite;
    const blackH = r.height * BLACK_HEIGHT;
    if (y < blackH) {
      const b = Math.round(x / ww);
      const midi = layout.blackAtBoundary.get(b);
      if (midi !== undefined && Math.abs(x - b * ww) <= (ww * BLACK_WIDTH) / 2) {
        return { midi, velocity: Math.round(1 + clamp(y / blackH, 0, 1) * 126) };
      }
    }
    const wi = clamp(Math.floor(x / ww), 0, nWhite - 1);
    return { midi: layout.whites[wi].midi, velocity: Math.round(1 + clamp(y / r.height, 0, 1) * 126) };
  };

  const press = (midi, velocity) => {
    const c = downCount.current.get(midi) || 0;
    downCount.current.set(midi, c + 1);
    keyEls.current.get(midi)?.classList.add("is-down");
    playNote(midi, velocity);
  };

  const release = (midi) => {
    const c = (downCount.current.get(midi) || 0) - 1;
    if (c > 0) {
      downCount.current.set(midi, c);
      return;
    }
    downCount.current.delete(midi);
    keyEls.current.get(midi)?.classList.remove("is-down");
    releaseNote(midi);
  };

  const showReadout = (hit) => {
    const el = readoutRef.current;
    if (!el) return;
    if (!hit) {
      el.textContent = "";
      return;
    }
    const d = velocityToDistance(hit.velocity, getParamValue("storm_distance"));
    const delay = d / SPEED_OF_SOUND;
    el.textContent = `${noteName(hit.midi)} · V ${hit.velocity} · strike ${formatDistance(d)} · thunder after ${delay.toFixed(1)} s`;
  };

  const onPointerDown = (e) => {
    if (e.button !== 0) return;
    const hit = hitTest(e.clientX, e.clientY);
    if (!hit) return;
    e.preventDefault();
    try {
      bedRef.current.setPointerCapture(e.pointerId);
    } catch {
      /* ignore */
    }
    pointers.current.set(e.pointerId, hit.midi);
    press(hit.midi, hit.velocity);
    showReadout(hit);
  };

  const onPointerMove = (e) => {
    const hit = hitTest(e.clientX, e.clientY);
    showReadout(hit);
    const held = pointers.current.get(e.pointerId);
    if (held === undefined) return;
    // Glissando: dragging across keys releases the old key and strikes the new one.
    if (hit && hit.midi !== held) {
      release(held);
      pointers.current.set(e.pointerId, hit.midi);
      press(hit.midi, hit.velocity);
    }
  };

  const endPointer = (e) => {
    const held = pointers.current.get(e.pointerId);
    if (held === undefined) return;
    pointers.current.delete(e.pointerId);
    release(held);
  };

  // Release everything if the window loses focus mid-press.
  useEffect(() => {
    const releaseAll = () => {
      for (const [, midi] of pointers.current) release(midi);
      pointers.current.clear();
    };
    window.addEventListener("blur", releaseAll);
    return () => {
      window.removeEventListener("blur", releaseAll);
      releaseAll();
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Glow from telemetry keyLevels, lightning-yellow flash on the struck key. Direct DOM writes, no re-render.
  useEffect(() => {
    const prev = new Float32Array(HIGHEST_KEY - LOWEST_KEY + 1).fill(-1);
    let lastStrikes = null;
    const apply = (t) => {
      const levels = t.keyLevels;
      for (let i = 0; i < prev.length; i++) {
        // sqrt: soft notes still glow visibly, loud ones saturate.
        const v = Math.round(Math.sqrt(clamp(levels[i] || 0, 0, 1)) * 100) / 100;
        if (v !== prev[i]) {
          prev[i] = v;
          keyEls.current.get(LOWEST_KEY + i)?.style.setProperty("--lvl", String(v));
        }
      }
      if (!t.received) return;
      if (lastStrikes === null || t.strikeCount < lastStrikes) lastStrikes = t.strikeCount;
      else if (t.strikeCount > lastStrikes) {
        lastStrikes = t.strikeCount;
        const el = keyEls.current.get(Math.round(t.lastStrikeKey));
        if (el) {
          el.classList.remove("is-struck");
          void el.offsetWidth; // restart the animation
          el.classList.add("is-struck");
        }
      }
    };
    apply(getTelemetry());
    return subscribeTelemetry(apply);
  }, []);

  const setKeyRef = (midi) => (el) => {
    if (el) keyEls.current.set(midi, el);
    else keyEls.current.delete(midi);
  };

  const ww = 100 / nWhite;

  return (
    <div className="flex shrink-0 flex-col gap-[0.3rem]">
      <div className="flex items-center justify-between gap-4 px-1 text-[0.64rem] leading-none">
        <div className="flex min-w-0 items-center gap-2 text-mist-400">
          <svg viewBox="0 0 12 22" className="h-[1.35em] w-auto shrink-0" aria-hidden="true">
            <defs>
              <linearGradient id="vel-grad" x1="0" y1="0" x2="0" y2="1">
                <stop offset="0%" stopColor="#43536a" />
                <stop offset="100%" stopColor="#f3e188" />
              </linearGradient>
            </defs>
            <rect x="3" y="1" width="6" height="20" rx="1.5" fill="url(#vel-grad)" />
          </svg>
          <span className="truncate">
            <span className="text-mist-300">Strike low on a key</span> for a hard hit: a close, overhead crack.{" "}
            <span className="text-mist-300">Strike high</span> for a soft hit: distant, muffled thunder.
          </span>
        </div>
        <div ref={readoutRef} className="num shrink-0 text-right text-rain-300" aria-live="off" data-testid="key-readout" />
      </div>
      <div
        ref={bedRef}
        className="keybed"
        style={{ height: "clamp(52px, 10.5vh, 132px)" }}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={endPointer}
        onPointerCancel={endPointer}
        onPointerLeave={() => showReadout(null)}
        role="group"
        aria-label="88-key keyboard, A0 to C8. Click lower on a key for a louder, closer strike."
      >
        {layout.whites.map((k) => (
          <div
            key={k.midi}
            ref={setKeyRef(k.midi)}
            className="key key--white"
            data-key={k.midi}
            style={{ left: `${k.index * ww}%`, width: `${ww}%` }}
          >
            <div className="glow" />
            <div className="strike" />
            {k.midi % 12 === 0 ? <div className="octave">{noteName(k.midi)}</div> : null}
          </div>
        ))}
        {layout.blacks.map((k) => (
          <div
            key={k.midi}
            ref={setKeyRef(k.midi)}
            className="key key--black"
            data-key={k.midi}
            style={{ left: `${(k.boundary - BLACK_WIDTH / 2) * ww}%`, width: `${BLACK_WIDTH * ww}%` }}
          >
            <div className="glow" />
            <div className="strike" />
          </div>
        ))}
      </div>
    </div>
  );
}
