// Petrichor Piano in the browser: stands in for the plug-in's JUCE backend so the real UI runs
// unchanged, and feeds the WebAssembly engine (worklet.js) from the UI, the computer keyboard and
// Web MIDI. Expects window.__PETRICHOR_WASM__ (base64) and window.__PETRICHOR_WORKLET__ (source).
(function () {
  "use strict";

  const TELEMETRY_FIELDS = [
    "windSpeed", "windGust", "gustFactor", "rainRate", "lambda", "grainRate", "meanDropMM", "impactSpeed",
    "activeVoices", "strikeCount", "lastStrikeDistance", "lastStrikeKey", "lastStrikeVelocity",
  ];
  const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

  //==============================================================================================
  // The JUCE backend the UI expects (window.__JUCE__.backend), routed to the engine.

  const listeners = new Map();
  const latestParams = new Map(); // what the UI has set, replayed when the engine starts
  let engine = null;              // { post(message), mode }

  function dispatch(id, payload) {
    const set = listeners.get(id);
    if (!set) return;
    for (const fn of Array.from(set)) {
      try {
        fn(payload);
      } catch (err) {
        console.warn("[petrichor] listener for " + id + " threw", err);
      }
    }
  }

  function post(message) {
    if (engine) engine.post(message);
  }

  function route(id, payload) {
    if (typeof id !== "string" || !payload || typeof payload !== "object") return;
    if (id.indexOf("__juce__slider") === 0) {
      const name = id.slice("__juce__slider".length);
      if (payload.eventType === "valueChanged") {
        const value = Number(payload.value);
        if (Number.isFinite(value)) {
          latestParams.set(name, value);
          post({ type: "param", id: name, value });
        }
      }
      return;
    }
    if (id === "__juce__invoke") {
      const args = Array.isArray(payload.params) ? payload.params : [];
      if (payload.name === "noteOn") noteOn(args[0], args[1]);
      else if (payload.name === "noteOff") noteOff(args[0]);
      setTimeout(() => dispatch("__juce__complete", { promiseId: payload.resultId, result: null }), 0);
    }
  }

  window.__JUCE__ = {
    backend: {
      addEventListener(id, fn) {
        if (!listeners.has(id)) listeners.set(id, new Set());
        listeners.get(id).add(fn);
        return [id, fn];
      },
      removeEventListener(token) {
        if (Array.isArray(token) && listeners.has(token[0])) listeners.get(token[0]).delete(token[1]);
      },
      emitEvent: route,
    },
    initialisationData: { __juce__functions: ["noteOn", "noteOff"] },
  };

  function onTelemetry(values) {
    const t = {};
    TELEMETRY_FIELDS.forEach((name, i) => (t[name] = values[i]));
    t.keyLevels = Array.from(values.subarray ? values.subarray(13, 101) : values.slice(13, 101));
    dispatch("telemetry", t);
  }

  //==============================================================================================
  // Notes and pedals

  function noteOn(key, velocity) {
    post({ type: "noteOn", key: clamp(Math.round(key), 0, 127), velocity: clamp(Math.round(velocity), 1, 127) });
  }
  function noteOff(key) {
    post({ type: "noteOff", key: clamp(Math.round(key), 0, 127) });
  }

  // Computer keyboard, by physical key: A S D F G H J K L ; ' are the white keys from C, W E T Y U O P the black.
  const KEY_OFFSETS = {
    KeyA: 0, KeyW: 1, KeyS: 2, KeyE: 3, KeyD: 4, KeyF: 5, KeyT: 6, KeyG: 7, KeyY: 8, KeyH: 9,
    KeyU: 10, KeyJ: 11, KeyK: 12, KeyO: 13, KeyL: 14, KeyP: 15, Semicolon: 16, Quote: 17,
  };
  const NOTE_NAMES = ["C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"];
  const state = { octave: 60, velocity: 90, pedal: false, midi: "not connected" };
  const heldKeys = new Map();

  function typing(target) {
    return target && (target.isContentEditable || /^(INPUT|TEXTAREA|SELECT)$/.test(target.tagName));
  }

  function setPedal(down) {
    if (state.pedal === down) return;
    state.pedal = down;
    post({ type: "sustain", down });
    updateChip();
  }

  window.addEventListener("keydown", (e) => {
    if (!engine || e.ctrlKey || e.metaKey || e.altKey || typing(e.target)) return;
    const offset = KEY_OFFSETS[e.code];
    if (offset !== undefined) {
      e.preventDefault();
      if (e.repeat || heldKeys.has(e.code)) return;
      const key = state.octave + offset;
      if (key < 21 || key > 108) return;
      heldKeys.set(e.code, key);
      noteOn(key, state.velocity);
      return;
    }
    switch (e.code) {
      case "KeyZ": state.octave = Math.max(24, state.octave - 12); break;
      case "KeyX": state.octave = Math.min(96, state.octave + 12); break;
      case "KeyC": state.velocity = Math.max(10, state.velocity - 15); break;
      case "KeyV": state.velocity = Math.min(127, state.velocity + 15); break;
      case "Space": e.preventDefault(); setPedal(true); return;
      default: return;
    }
    e.preventDefault();
    updateChip();
  });

  window.addEventListener("keyup", (e) => {
    if (heldKeys.has(e.code)) {
      noteOff(heldKeys.get(e.code));
      heldKeys.delete(e.code);
    } else if (e.code === "Space") {
      setPedal(false);
    }
  });

  window.addEventListener("blur", () => {
    for (const key of heldKeys.values()) noteOff(key);
    heldKeys.clear();
    setPedal(false);
  });

  function connectMidi() {
    if (!navigator.requestMIDIAccess) {
      state.midi = "not available in this browser";
      updateChip();
      return;
    }
    navigator.requestMIDIAccess().then(
      (access) => {
        const attach = () => {
          let count = 0;
          for (const input of access.inputs.values()) {
            input.onmidimessage = onMidi;
            count++;
          }
          state.midi = count ? count + " device" + (count > 1 ? "s" : "") : "no devices";
          updateChip();
        };
        attach();
        access.onstatechange = attach;
      },
      () => {
        state.midi = "blocked by the page";
        updateChip();
      }
    );
  }

  function onMidi(event) {
    const [status, d1, d2] = event.data;
    const type = status & 0xf0;
    if (type === 0x90 && d2 > 0) noteOn(d1, d2);
    else if (type === 0x80 || type === 0x90) noteOff(d1);
    else if (type === 0xb0) {
      if (d1 === 64) setPedal(d2 >= 64);
      else if (d1 === 67) post({ type: "soft", down: d2 >= 64 });
      else if (d1 === 120 || d1 === 123) post({ type: "allNotesOff" });
    }
  }

  //==============================================================================================
  // Audio: the engine in an AudioWorklet, or on the main thread if the worklet is refused.

  function decodeBase64(text) {
    const binary = atob(text);
    const bytes = new Uint8Array(binary.length);
    for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
    return bytes;
  }

  async function startAudio() {
    const bytes = decodeBase64(window.__PETRICHOR_WASM__);
    const Context = window.AudioContext || window.webkitAudioContext;
    const ctx = new Context({ latencyHint: "interactive" });
    await ctx.resume();

    try {
      if (!ctx.audioWorklet) throw new Error("AudioWorklet is not supported");
      const url = URL.createObjectURL(new Blob([window.__PETRICHOR_WORKLET__], { type: "application/javascript" }));
      await ctx.audioWorklet.addModule(url);
      const node = new AudioWorkletNode(ctx, "petrichor", {
        numberOfInputs: 0,
        numberOfOutputs: 1,
        outputChannelCount: [2],
        processorOptions: { wasm: bytes.buffer.slice(0) },
      });
      await new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error("the audio engine did not start in time")), 10000);
        node.port.onmessage = (event) => {
          const m = event.data;
          if (m.type === "telemetry") onTelemetry(m.values);
          else if (m.type === "ready") { clearTimeout(timer); resolve(); }
          else if (m.type === "error") { clearTimeout(timer); reject(new Error(m.message)); }
        };
      });
      node.connect(ctx.destination);
      engine = { post: (m) => node.port.postMessage(m), mode: "AudioWorklet" };
    } catch (workletError) {
      console.warn("[petrichor] AudioWorklet path failed, using the main thread:", workletError);
      const core = await petrichorEngineAsync(bytes.buffer, ctx.sampleRate);
      const processor = ctx.createScriptProcessor(1024, 0, 2);
      let sinceTelemetry = 0;
      processor.onaudioprocess = (event) => {
        const left = event.outputBuffer.getChannelData(0);
        const right = event.outputBuffer.getChannelData(1);
        for (let at = 0; at < left.length; at += 128)
          core.render(left.subarray(at, at + 128), right.subarray(at, at + 128));
        sinceTelemetry += left.length;
        if (sinceTelemetry >= ctx.sampleRate / 30) {
          sinceTelemetry = 0;
          onTelemetry(core.telemetry());
        }
      };
      processor.connect(ctx.destination);
      engine = { post: (m) => core.handle(m), mode: "main thread" };
    }

    for (const [id, value] of latestParams) engine.post({ type: "param", id, value });
    state.rate = ctx.sampleRate;
    connectMidi();
  }

  //==============================================================================================
  // Start screen and the small status chip.

  const style = document.createElement("style");
  style.textContent = `
    :root { --pw-ink: #05080e; --pw-panel: #0e1624; --pw-line: #2a3d5a; --pw-fg: #e3edf5; --pw-muted: #9fb2c6; --pw-bolt: #f3e188; --pw-error: #f4a3a3; }
    .pw-overlay { position: fixed; inset: 0; z-index: 1000; display: grid; place-items: center; padding: 16px;
      background: radial-gradient(ellipse at 50% 30%, rgba(20,32,51,0.92), rgba(5,8,14,0.97)); color: var(--pw-fg);
      font-family: Manrope, system-ui, sans-serif; }
    .pw-card { width: min(34rem, 100%); display: grid; gap: 1rem; }
    .pw-eyebrow { font: 600 0.7rem/1 "IBM Plex Mono", ui-monospace, monospace; letter-spacing: 0.14em; text-transform: uppercase; color: var(--pw-muted); }
    .pw-title { margin: 0; font: 600 clamp(2.4rem, 7vw, 3.4rem)/1 "Cormorant Garamond", Georgia, serif; text-wrap: balance; }
    .pw-lede { margin: 0; color: var(--pw-muted); line-height: 1.55; max-width: 60ch; }
    .pw-start { justify-self: start; padding: 0.8rem 1.5rem; border-radius: 999px; border: 1px solid var(--pw-bolt);
      background: var(--pw-bolt); color: var(--pw-ink); font: 700 0.95rem/1 Manrope, system-ui, sans-serif; cursor: pointer; }
    .pw-start:hover { background: #fbf1b5; }
    .pw-start:focus-visible { outline: 2px solid var(--pw-fg); outline-offset: 3px; }
    .pw-start[disabled] { opacity: 0.6; cursor: progress; }
    .pw-keys { display: grid; grid-template-columns: max-content 1fr; gap: 0.35rem 1rem; margin: 0; font-size: 0.85rem; color: var(--pw-muted); }
    .pw-keys dt { font-family: "IBM Plex Mono", ui-monospace, monospace; color: var(--pw-fg); }
    .pw-keys dd { margin: 0; }
    .pw-credit { margin: 0; font-size: 0.75rem; color: var(--pw-muted); line-height: 1.5; }
    .pw-credit a { color: var(--pw-fg); }
    .pw-error { margin: 0; color: var(--pw-error); font-size: 0.85rem; line-height: 1.5; }
    .pw-chip { position: fixed; right: max(12px, env(safe-area-inset-right, 0px)); bottom: calc(12px + env(safe-area-inset-bottom, 0px)); z-index: 999;
      padding: 0.45rem 0.75rem; border-radius: 999px; border: 1px solid var(--pw-line); background: rgba(14,22,36,0.88); color: var(--pw-muted);
      font: 500 0.7rem/1.2 "IBM Plex Mono", ui-monospace, monospace; font-variant-numeric: tabular-nums; pointer-events: none; max-width: calc(100% - 24px);
      opacity: 1; transition: opacity 0.6s ease; }
    .pw-chip.pw-quiet { opacity: 0; }
    .pw-chip b { color: var(--pw-fg); font-weight: 600; }
    @media (prefers-reduced-motion: no-preference) { .pw-overlay { transition: opacity 0.4s ease; } }
  `;

  let chip = null, chipTimer = null;
  function updateChip() {
    if (!chip) return;
    // Shown for a few seconds after each change, then out of the way of the keyboard.
    chip.classList.remove("pw-quiet");
    clearTimeout(chipTimer);
    chipTimer = setTimeout(() => chip.classList.add("pw-quiet"), 3000);
    const octaveName = NOTE_NAMES[state.octave % 12] + (Math.floor(state.octave / 12) - 1);
    chip.innerHTML =
      `keys from <b>${octaveName}</b> · velocity <b>${state.velocity}</b> · pedal <b>${state.pedal ? "down" : "up"}</b>` +
      ` · MIDI <b>${state.midi}</b>`;
  }

  function buildOverlay() {
    document.head.appendChild(style);
    const overlay = document.createElement("div");
    overlay.className = "pw-overlay";
    overlay.innerHTML = `
      <div class="pw-card" role="dialog" aria-labelledby="pw-title">
        <div class="pw-eyebrow">Browser test build</div>
        <h1 class="pw-title" id="pw-title">Petrichor Piano</h1>
        <p class="pw-lede">The plug-in's own engine, compiled to WebAssembly and running in this page, with the same controls. Use headphones or decent speakers: the thunder lives in the low strings.</p>
        <dl class="pw-keys">
          <dt>Mouse</dt><dd>click or drag across the keyboard at the bottom; turn any knob</dd>
          <dt>A S D F … ; '</dt><dd>white keys from C4, W E T Y U O P the black keys</dd>
          <dt>Z / X</dt><dd>octave down / up</dd>
          <dt>C / V</dt><dd>softer / harder (soft = distant thunder)</dd>
          <dt>Space</dt><dd>sustain pedal, while held</dd>
          <dt>MIDI</dt><dd>a connected keyboard plays too, if the browser allows Web MIDI here</dd>
        </dl>
        <button class="pw-start" id="pw-start" type="button">Start the storm</button>
        <p class="pw-error" id="pw-error" hidden></p>
        <p class="pw-credit">Piano I is resynthesised from the <a href="https://creativecommons.org/licenses/by/3.0/" target="_blank" rel="noopener">CC-BY 3.0</a> Salamander Grand Piano V3 by Alexander Holm.</p>
      </div>`;
    document.body.appendChild(overlay);

    const button = overlay.querySelector("#pw-start");
    const error = overlay.querySelector("#pw-error");
    button.focus({ preventScroll: true });
    button.addEventListener("click", async () => {
      button.disabled = true;
      button.textContent = "Starting…";
      error.hidden = true;
      try {
        await startAudio();
        overlay.style.opacity = "0";
        setTimeout(() => overlay.remove(), 400);
        chip = document.createElement("div");
        chip.className = "pw-chip";
        chip.setAttribute("aria-live", "polite");
        document.body.appendChild(chip);
        updateChip();
      } catch (err) {
        console.error("[petrichor] could not start the engine", err);
        error.textContent = "The audio engine could not start in this browser: " + ((err && err.message) || err) +
          ". Try a current Chrome, Edge, Firefox or Safari.";
        error.hidden = false;
        button.disabled = false;
        button.textContent = "Try again";
      }
    });
  }

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", buildOverlay);
  else buildOverlay();
})();
