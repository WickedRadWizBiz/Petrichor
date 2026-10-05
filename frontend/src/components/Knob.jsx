import React, { useEffect, useRef, useState } from "react";
import { getParam, useParam } from "../juce";
import { clamp, formatValue, toNormalised } from "../params";
import { hideTip, showTip } from "./Tooltip";

export const ACCENTS = {
  bolt: { stroke: "#f3e188", glow: "rgba(243,225,136,0.35)", text: "text-bolt-300" },
  rain: { stroke: "#7dd3e8", glow: "rgba(125,211,232,0.35)", text: "text-rain-300" },
  earth: { stroke: "#d4a373", glow: "rgba(212,163,115,0.35)", text: "text-earth-300" },
  mist: { stroke: "#c5d4e2", glow: "rgba(197,212,226,0.3)", text: "text-mist-100" },
};

const START = -135; // degrees from 12 o'clock
const SWEEP = 270;
const R_TRACK = 42;

function polar(deg, r) {
  const a = (deg * Math.PI) / 180;
  return [50 + r * Math.sin(a), 50 - r * Math.cos(a)];
}

function arcPath(n0, n1, r) {
  const a0 = START + SWEEP * clamp(Math.min(n0, n1), 0, 1);
  const a1 = START + SWEEP * clamp(Math.max(n0, n1), 0, 1);
  if (a1 - a0 < 0.05) return "";
  const [x0, y0] = polar(a0, r);
  const [x1, y1] = polar(a1, r);
  const large = a1 - a0 > 180 ? 1 : 0;
  return `M ${x0.toFixed(2)} ${y0.toFixed(2)} A ${r} ${r} 0 ${large} 1 ${x1.toFixed(2)} ${y1.toFixed(2)}`;
}

const DRAG_PIXELS = 190; // full range for a plain vertical drag
const FINE = 0.1; // shift

/** Shared SVG gradients for all knobs (rendered once). */
export function KnobDefs() {
  return (
    <svg width="0" height="0" style={{ position: "absolute" }} aria-hidden="true" focusable="false">
      <defs>
        <radialGradient id="knob-body" cx="38%" cy="30%" r="75%">
          <stop offset="0%" stopColor="#334761" />
          <stop offset="55%" stopColor="#18243a" />
          <stop offset="100%" stopColor="#0a111c" />
        </radialGradient>
        <linearGradient id="knob-rim" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="rgba(197,212,226,0.35)" />
          <stop offset="100%" stopColor="rgba(197,212,226,0.02)" />
        </linearGradient>
        <radialGradient id="knob-sheen" cx="35%" cy="22%" r="45%">
          <stop offset="0%" stopColor="rgba(227,237,245,0.28)" />
          <stop offset="100%" stopColor="rgba(227,237,245,0)" />
        </radialGradient>
      </defs>
    </svg>
  );
}

export default function Knob({ id, accent = "rain", large = false }) {
  const p = useParam(id);
  const { spec } = p;
  const colors = ACCENTS[accent] || ACCENTS.rain;
  const ref = useRef(null);
  const drag = useRef(null);
  const pulse = useRef(null);
  const hoverTimer = useRef(null);
  const [active, setActive] = useState(false);

  const formatted = formatValue(spec, p.value);
  const n = p.normalised;
  const origin = toNormalised(spec.origin ?? spec.min, p.range);

  const tipFor = () => ({
    owner: id,
    title: spec.name,
    value: formatValue(spec, getParam(id).value),
    text: spec.desc,
    rect: ref.current ? ref.current.getBoundingClientRect() : { left: 0, top: 0, width: 0, height: 0, bottom: 0 },
  });

  // Keep the tooltip's value live while it is showing for this knob.
  useEffect(() => {
    if (active) showTip(tipFor());
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [p.value, active]);

  // Wheel and keyboard edits are grouped into one host gesture that ends after a short pause.
  const nudge = (fn) => {
    const st = getParam(id);
    if (pulse.current) clearTimeout(pulse.current);
    else st.dragStart();
    fn(st);
    pulse.current = setTimeout(() => {
      pulse.current = null;
      st.dragEnd();
    }, 350);
  };

  useEffect(() => {
    const el = ref.current;
    if (!el) return undefined;
    const onWheel = (e) => {
      e.preventDefault();
      const raw = Math.abs(e.deltaY) >= Math.abs(e.deltaX) ? e.deltaY : e.deltaX;
      const px = e.deltaMode === 1 ? raw * 33 : e.deltaMode === 2 ? raw * 400 : raw;
      const step = clamp(-px * 0.00028, -0.06, 0.06) * (e.shiftKey ? FINE : 1);
      nudge((st) => st.setNormalised(clamp(st.normalised + step, 0, 1)));
    };
    el.addEventListener("wheel", onWheel, { passive: false });
    return () => {
      el.removeEventListener("wheel", onWheel);
      if (pulse.current) {
        clearTimeout(pulse.current);
        pulse.current = null;
        getParam(id).dragEnd();
      }
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [id]);

  useEffect(() => () => clearTimeout(hoverTimer.current), []);

  const onPointerDown = (e) => {
    if (e.button !== 0 || drag.current) return;
    e.preventDefault();
    ref.current.focus({ preventScroll: true });
    try {
      e.currentTarget.setPointerCapture(e.pointerId);
    } catch {
      /* pointer already gone */
    }
    drag.current = { id: e.pointerId, y: e.clientY, n: p.normalised };
    p.dragStart();
    clearTimeout(hoverTimer.current);
    setActive(true);
  };

  const onPointerMove = (e) => {
    const d = drag.current;
    if (!d || d.id !== e.pointerId) return;
    const dy = d.y - e.clientY;
    d.y = e.clientY;
    d.n = clamp(d.n + (dy / DRAG_PIXELS) * (e.shiftKey ? FINE : 1), 0, 1);
    p.setNormalised(d.n);
  };

  const endDrag = (e) => {
    const d = drag.current;
    if (!d || (e && e.pointerId !== undefined && d.id !== e.pointerId)) return;
    drag.current = null;
    p.dragEnd();
    const hovering = ref.current && ref.current.matches(":hover");
    if (!hovering && document.activeElement !== ref.current) {
      setActive(false);
      hideTip(id);
    }
  };

  const onKeyDown = (e) => {
    const fine = e.shiftKey ? FINE : 1;
    let delta = null;
    let absolute = null;
    switch (e.key) {
      case "ArrowUp":
      case "ArrowRight":
        delta = 0.01 * fine;
        break;
      case "ArrowDown":
      case "ArrowLeft":
        delta = -0.01 * fine;
        break;
      case "PageUp":
        delta = 0.1;
        break;
      case "PageDown":
        delta = -0.1;
        break;
      case "Home":
        absolute = 0;
        break;
      case "End":
        absolute = 1;
        break;
      default:
        return;
    }
    e.preventDefault();
    nudge((st) => st.setNormalised(absolute !== null ? absolute : clamp(st.normalised + delta, 0, 1)));
  };

  const onPointerEnter = () => {
    clearTimeout(hoverTimer.current);
    hoverTimer.current = setTimeout(() => setActive(true), 280);
  };
  const onPointerLeave = () => {
    clearTimeout(hoverTimer.current);
    if (drag.current) return;
    if (document.activeElement === ref.current && ref.current.matches(":focus-visible")) return;
    setActive(false);
    hideTip(id);
  };
  const onFocus = () => {
    if (ref.current && ref.current.matches(":focus-visible")) setActive(true);
  };
  const onBlur = () => {
    if (drag.current) return;
    setActive(false);
    hideTip(id);
  };

  const [ix0, iy0] = polar(START + SWEEP * n, 15);
  const [ix1, iy1] = polar(START + SWEEP * n, 29);
  const [dx, dy] = polar(START + SWEEP * n, R_TRACK);
  const valueText = formatted.replace("−", "-");
  const dragging = !!drag.current;

  return (
    <div className="flex min-w-0 flex-col items-center gap-[0.18rem]">
      <div className={`eyebrow max-w-full truncate !text-[0.58rem] ${active ? "!text-mist-300" : ""}`} aria-hidden="true">
        {spec.short}
      </div>
      <div
        ref={ref}
        className={`knob ${large ? "knob--lg" : ""}`}
        role="slider"
        tabIndex={0}
        aria-label={spec.name}
        aria-valuemin={spec.min}
        aria-valuemax={spec.max}
        aria-valuenow={Number(p.value.toFixed(4))}
        aria-valuetext={valueText}
        aria-describedby={undefined}
        data-param={id}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={endDrag}
        onPointerCancel={endDrag}
        onLostPointerCapture={endDrag}
        onDoubleClick={() => p.reset()}
        onKeyDown={onKeyDown}
        onPointerEnter={onPointerEnter}
        onPointerLeave={onPointerLeave}
        onFocus={onFocus}
        onBlur={onBlur}
      >
        <svg viewBox="0 0 100 100" aria-hidden="true">
          {/* track */}
          <path d={arcPath(0, 1, R_TRACK)} fill="none" stroke="rgba(122,142,166,0.16)" strokeWidth="5.5" strokeLinecap="round" />
          {/* value glow + arc */}
          <path d={arcPath(origin, n, R_TRACK)} fill="none" stroke={colors.glow} strokeWidth="11" strokeLinecap="round" opacity={active || dragging ? 0.9 : 0.45} />
          <path className="value-arc" d={arcPath(origin, n, R_TRACK)} fill="none" stroke={colors.stroke} strokeWidth="5.5" strokeLinecap="round" />
          {/* body */}
          <circle cx="50" cy="50" r="32" fill="url(#knob-body)" />
          <circle cx="50" cy="50" r="32" fill="none" stroke="url(#knob-rim)" strokeWidth="1.2" />
          <circle cx="50" cy="50" r="31" fill="url(#knob-sheen)" />
          {/* pointer */}
          <line x1={ix0} y1={iy0} x2={ix1} y2={iy1} stroke={colors.stroke} strokeWidth="4.2" strokeLinecap="round" />
          <circle cx={dx} cy={dy} r="3.4" fill="#e3edf5" opacity={active || dragging ? 1 : 0.85} />
        </svg>
      </div>
      <div className={`num max-w-full truncate text-[0.68rem] leading-none ${active ? colors.text : "text-mist-300"}`} aria-hidden="true">
        {formatted}
      </div>
    </div>
  );
}
