import React from "react";
import { GROUPS, modeRelevance } from "../params";
import { useParam } from "../juce";
import BlendSlider from "./BlendSlider";
import Knob from "./Knob";

const TITLE_COLOR = {
  bolt: "text-bolt-300",
  rain: "text-rain-300",
  earth: "text-earth-300",
  mist: "text-mist-100",
};

const DOT_COLOR = {
  bolt: "#f3e188",
  rain: "#7dd3e8",
  earth: "#d4a373",
  mist: "#c5d4e2",
};

// Captions with real sub/superscripts (plain-text versions live in params.js).
const CAPTIONS = {
  thunder: <>Always fused: the strike is the lightning, x &prop; (127 &minus; V)</>,
  wind: <>Fused notes bend like wind pitch, f &prop; U</>,
  rain: (
    <>
      Gusts raise R; &Lambda; = 4.1&thinsp;R<sup className="text-[0.5rem]">&minus;0.21</sup>
    </>
  ),
  piano: <>I: a real grand, resynthesised &middot; II: a Rhodes-style tine</>,
  master: <>Output gain</>,
};

function GroupGlyph({ group }) {
  const c = DOT_COLOR[group.accent];
  switch (group.id) {
    case "thunder":
      return (
        <svg viewBox="0 0 16 16" className="h-[0.95em] w-[0.95em] shrink-0" aria-hidden="true">
          <path d="M9.6 1 3.8 9h3.6L6.2 15l6-8.2H8.6L9.6 1Z" fill={c} opacity="0.9" />
        </svg>
      );
    case "wind":
      return (
        <svg viewBox="0 0 16 16" className="h-[0.95em] w-[0.95em] shrink-0" aria-hidden="true" fill="none" stroke={c} strokeWidth="1.4" strokeLinecap="round">
          <path d="M1.5 6h8.2a2.2 2.2 0 1 0-2.1-2.9" />
          <path d="M1.5 10h11a2.2 2.2 0 1 1-2.1 2.9" />
        </svg>
      );
    case "rain":
      return (
        <svg viewBox="0 0 16 16" className="h-[0.95em] w-[0.95em] shrink-0" aria-hidden="true">
          <path d="M8 1.5C8 1.5 3.4 7 3.4 10a4.6 4.6 0 0 0 9.2 0C12.6 7 8 1.5 8 1.5Z" fill="none" stroke={c} strokeWidth="1.4" />
        </svg>
      );
    case "piano":
      return (
        <svg viewBox="0 0 16 16" className="h-[0.95em] w-[0.95em] shrink-0" aria-hidden="true" fill="none" stroke={c} strokeWidth="1.3">
          <rect x="1.5" y="2.5" width="13" height="11" rx="1.5" />
          <path d="M5.8 2.5v11M10.2 2.5v11" />
          <path d="M4.6 2.5v6M9 2.5v6M13.2 2.5v6" strokeWidth="2.2" strokeLinecap="round" />
        </svg>
      );
    default:
      return (
        <svg viewBox="0 0 16 16" className="h-[0.95em] w-[0.95em] shrink-0" aria-hidden="true">
          <circle cx="8" cy="8" r="5.5" fill="none" stroke={c} strokeWidth="1.4" />
          <circle cx="8" cy="8" r="1.8" fill={c} />
        </svg>
      );
  }
}

/** A knob that fades (but stays usable) when the Overlay/Fuse blend makes it irrelevant. */
function ModeKnob({ spec, blendId, accent }) {
  const blend = useParam(blendId);
  const relevance = modeRelevance(spec, blend.value);
  return (
    <div
      className="min-w-0 transition-opacity duration-150"
      style={{ opacity: 0.32 + 0.68 * relevance }}
      data-mode={spec.mode}
      title={relevance < 0.25 ? `Only heard toward ${spec.mode === "fuse" ? "Fuse" : spec.mode === "overlay" ? "Overlay" : spec.mode}` : undefined}
    >
      <Knob id={spec.id} accent={accent} />
    </div>
  );
}

function Group({ group }) {
  const large = group.id === "master";
  return (
    <section
      className="panel flex min-w-0 flex-col px-[0.7rem] pb-[0.55rem] pt-[0.5rem]"
      aria-label={`${group.title} controls`}
      data-group={group.id}
    >
      <header className="mb-[0.35rem] min-w-0" title={group.long}>
        <h2 className={`flex items-center gap-1.5 font-display text-[1.12rem] font-semibold leading-none tracking-wide ${TITLE_COLOR[group.accent]}`}>
          <GroupGlyph group={group} />
          <span className="truncate">{group.title}</span>
        </h2>
        <p className="mt-[0.2rem] line-clamp-2 text-[0.64rem] leading-snug text-mist-400">{CAPTIONS[group.id] ?? group.caption}</p>
      </header>
      {group.blend && <BlendSlider id={group.blend.id} accent={group.accent} />}
      <div
        className={`mt-auto grid ${large ? "flex-1 place-items-center" : "gap-x-[0.2rem] gap-y-[0.35rem]"}`}
        style={{ gridTemplateColumns: `repeat(${group.cols}, minmax(0, 1fr))` }}
      >
        {group.params.map((p) =>
          p.mode && group.blend ? (
            <ModeKnob key={p.id} spec={p} blendId={group.blend.id} accent={group.accent} />
          ) : (
            <Knob key={p.id} id={p.id} accent={group.accent} large={large} />
          ),
        )}
      </div>
    </section>
  );
}

export default function ControlDeck() {
  const template = GROUPS.map((g) => `minmax(0, ${g.id === "master" ? 1.35 : g.cols}fr)`).join(" ");
  return (
    <div className="grid shrink-0 gap-[var(--gap)]" style={{ gridTemplateColumns: template }}>
      {GROUPS.map((g) => (
        <Group key={g.id} group={g} />
      ))}
    </div>
  );
}
