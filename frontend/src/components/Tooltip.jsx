import React, { useLayoutEffect, useRef, useState, useSyncExternalStore } from "react";

// One floating, single-line tooltip shared by every control.

let current = null; // { owner, title, text, value, rect }
let version = 0;
const listeners = new Set();

function emit() {
  version++;
  for (const fn of listeners) fn();
}

export function showTip(tip) {
  current = tip;
  emit();
}

export function hideTip(owner) {
  if (current && (owner === undefined || current.owner === owner)) {
    current = null;
    emit();
  }
}

const subscribe = (fn) => {
  listeners.add(fn);
  return () => listeners.delete(fn);
};
const getVersion = () => version;

export function TooltipLayer() {
  useSyncExternalStore(subscribe, getVersion, getVersion);
  const tip = current;
  const ref = useRef(null);
  const [pos, setPos] = useState({ left: -9999, top: -9999, below: false });

  useLayoutEffect(() => {
    if (!tip || !ref.current) return;
    const el = ref.current;
    const w = el.offsetWidth;
    const h = el.offsetHeight;
    const vw = window.innerWidth;
    const r = tip.rect;
    const cx = r.left + r.width / 2;
    const left = Math.max(8, Math.min(vw - w - 8, cx - w / 2));
    let top = r.top - h - 10;
    let below = false;
    if (top < 6) {
      top = r.bottom + 10;
      below = true;
    }
    if (left !== pos.left || top !== pos.top || below !== pos.below) setPos({ left, top, below });
  });

  if (!tip) return null;
  return (
    <div
      ref={ref}
      role="tooltip"
      className="pointer-events-none fixed z-50 flex max-w-[min(860px,calc(100vw-16px))] items-baseline gap-2 overflow-hidden whitespace-nowrap rounded-lg px-3 py-1.5 text-[0.72rem] leading-tight text-mist-200"
      style={{
        left: pos.left,
        top: pos.top,
        background: "rgba(8, 13, 22, 0.94)",
        boxShadow: "0 0 0 1px rgba(122,142,166,0.22), 0 12px 30px -10px rgba(0,0,0,0.9)",
        backdropFilter: "blur(6px)",
      }}
    >
      <span className="font-bold text-mist-100">{tip.title}</span>
      {tip.value ? <span className="num text-rain-300">{tip.value}</span> : null}
      <span className="truncate text-mist-300">{tip.text}</span>
    </div>
  );
}
