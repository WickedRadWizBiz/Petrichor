import React, { useEffect, useRef, useState } from "react";
import { getParam, useParam } from "../juce";
import { clamp } from "../params";
import { ACCENTS } from "./Knob";
import { hideTip, showTip } from "./Tooltip";

/**
 * Overlay <-> Fuse crossfader. Left: the element is heard beside the piano. Right: it lives inside
 * the notes. Same gesture contract as the knobs (dragStart / setNormalised / dragEnd, double-click
 * reset, wheel, arrow keys).
 */
export default function BlendSlider({ id, accent = "rain" }) {
  const p = useParam(id);
  const { spec } = p;
  const colors = ACCENTS[accent] || ACCENTS.rain;
  const track = useRef(null);
  const drag = useRef(null);
  const pulse = useRef(null);
  const [active, setActive] = useState(false);

  const n = clamp(p.normalised, 0, 1);
  const fusePct = Math.round(n * 100);
  const label = fusePct <= 2 ? "Overlay" : fusePct >= 98 ? "Fuse" : `${100 - fusePct} / ${fusePct}`;

  const tip = () => ({
    owner: id,
    title: spec.name,
    value: label,
    text: spec.desc,
    rect: track.current ? track.current.getBoundingClientRect() : { left: 0, top: 0, width: 0, height: 0, bottom: 0 },
  });

  useEffect(() => {
    if (active) showTip(tip());
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [p.value, active]);

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
    const el = track.current;
    if (!el) return undefined;
    const onWheel = (e) => {
      e.preventDefault();
      const raw = Math.abs(e.deltaY) >= Math.abs(e.deltaX) ? -e.deltaY : e.deltaX;
      const px = e.deltaMode === 1 ? raw * 33 : e.deltaMode === 2 ? raw * 400 : raw;
      const step = clamp(px * 0.00028, -0.06, 0.06) * (e.shiftKey ? 0.1 : 1);
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

  const positionToNormalised = (clientX) => {
    const r = track.current.getBoundingClientRect();
    return clamp((clientX - r.left) / Math.max(r.width, 1), 0, 1);
  };

  const onPointerDown = (e) => {
    if (e.button !== 0 || drag.current) return;
    e.preventDefault();
    track.current.focus({ preventScroll: true });
    try {
      e.currentTarget.setPointerCapture(e.pointerId);
    } catch {
      /* pointer already gone */
    }
    drag.current = { id: e.pointerId, x: e.clientX, n: p.normalised, fine: e.shiftKey };
    p.dragStart();
    if (!e.shiftKey) p.setNormalised(positionToNormalised(e.clientX));
    setActive(true);
  };

  const onPointerMove = (e) => {
    const d = drag.current;
    if (!d || d.id !== e.pointerId) return;
    if (d.fine || e.shiftKey) {
      const r = track.current.getBoundingClientRect();
      d.n = clamp(d.n + ((e.clientX - d.x) / Math.max(r.width, 1)) * 0.1, 0, 1);
      d.x = e.clientX;
      p.setNormalised(d.n);
    } else {
      d.n = positionToNormalised(e.clientX);
      d.x = e.clientX;
      p.setNormalised(d.n);
    }
  };

  const endDrag = (e) => {
    const d = drag.current;
    if (!d || (e && e.pointerId !== undefined && d.id !== e.pointerId)) return;
    drag.current = null;
    p.dragEnd();
    if (!(track.current && track.current.matches(":hover"))) {
      setActive(false);
      hideTip(id);
    }
  };

  const onKeyDown = (e) => {
    const fine = e.shiftKey ? 0.1 : 1;
    let next = null;
    switch (e.key) {
      case "ArrowRight":
      case "ArrowUp":
        next = p.normalised + 0.02 * fine;
        break;
      case "ArrowLeft":
      case "ArrowDown":
        next = p.normalised - 0.02 * fine;
        break;
      case "Home":
        next = 0;
        break;
      case "End":
        next = 1;
        break;
      default:
        return;
    }
    e.preventDefault();
    nudge((st) => st.setNormalised(clamp(next, 0, 1)));
  };

  return (
    <div className="mb-[0.35rem] flex min-w-0 items-center gap-[0.45rem]" data-blend={id}>
      <span className={`eyebrow !text-[0.55rem] ${n < 0.5 ? "!text-mist-200" : ""}`} aria-hidden="true">
        Overlay
      </span>
      <div
        ref={track}
        className="blend-track relative h-[1.15rem] min-w-0 flex-1 cursor-ew-resize rounded-full outline-none"
        role="slider"
        tabIndex={0}
        aria-label={spec.name}
        aria-valuemin={0}
        aria-valuemax={1}
        aria-valuenow={Number(p.value.toFixed(4))}
        aria-valuetext={`${100 - fusePct}% overlay, ${fusePct}% fuse`}
        data-param={id}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={endDrag}
        onPointerCancel={endDrag}
        onLostPointerCapture={endDrag}
        onDoubleClick={() => p.reset()}
        onKeyDown={onKeyDown}
        onPointerEnter={() => setActive(true)}
        onPointerLeave={() => {
          if (drag.current) return;
          setActive(false);
          hideTip(id);
        }}
        onBlur={() => {
          if (drag.current) return;
          setActive(false);
          hideTip(id);
        }}
      >
        {/* groove: a layer beside the piano (left) melting into it (right) */}
        <div
          className="absolute inset-x-0 top-1/2 h-[0.32rem] -translate-y-1/2 rounded-full"
          style={{
            background: `linear-gradient(90deg, rgba(122,142,166,0.22) 0%, ${colors.glow} 100%)`,
          }}
        />
        <div
          className="absolute left-0 top-1/2 h-[0.32rem] -translate-y-1/2 rounded-full"
          style={{ width: `${n * 100}%`, background: colors.stroke, opacity: 0.75 }}
        />
        {/* puck */}
        <div
          className="absolute top-1/2 h-[1.05rem] w-[1.05rem] -translate-x-1/2 -translate-y-1/2 rounded-full border"
          style={{
            left: `${n * 100}%`,
            background: "radial-gradient(circle at 38% 30%, #334761, #0a111c)",
            borderColor: colors.stroke,
            boxShadow: active ? `0 0 0.6rem ${colors.glow}` : "none",
          }}
        />
      </div>
      <span className={`eyebrow !text-[0.55rem] ${n >= 0.5 ? colors.text : ""}`} aria-hidden="true">
        Fuse
      </span>
      <span className="num w-[3.4rem] shrink-0 text-right text-[0.62rem] text-mist-300" aria-hidden="true">
        {label}
      </span>
    </div>
  );
}
