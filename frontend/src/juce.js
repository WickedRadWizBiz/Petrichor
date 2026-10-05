// Bridge between the React UI and the JUCE 8 WebBrowserComponent backend.
//
// Protocol (juce_gui_extra/native/javascript/index.js):
//   window.__JUCE__.backend.emitEvent(name, payload) / addEventListener(name, fn) / removeEventListener
//   sliders: event id "__juce__slider" + relayName
//     -> {eventType: "requestInitialUpdate"} | {eventType: "valueChanged", value: scaled}
//        {eventType: "sliderDragStarted"} | {eventType: "sliderDragEnded"}
//     <- {eventType: "valueChanged", value: scaled}
//        {eventType: "propertiesChanged", start, end, skew, name, label, numSteps, interval, parameterIndex}
//   native functions: emitEvent("__juce__invoke", {name, params, resultId}) <- "__juce__complete" {promiseId, result}
//   telemetry: event "telemetry" (~30 Hz)
//
// Without window.__JUCE__ (plain browser / dev server) a mock backend with the same parameter
// ranges and a small storm simulation stands in, so the UI is fully playable.

import { useEffect, useState, useSyncExternalStore } from "react";
import {
  PARAMS,
  PARAM_BY_ID,
  clamp,
  defaultRange,
  fromNormalised,
  snapToInterval,
  toNormalised,
  LOWEST_KEY,
  HIGHEST_KEY,
} from "./params";
import { createMockBackend } from "./storm/mockBackend";

//==============================================================================
// Backend detection

function detectBackend() {
  if (typeof window === "undefined") return null;
  const j = window.__JUCE__;
  const backend = j && j.backend;
  if (!backend || typeof backend.emitEvent !== "function" || typeof backend.addEventListener !== "function")
    return null;
  const init = j.initialisationData && typeof j.initialisationData === "object" ? j.initialisationData : {};
  return { backend, init };
}

const juce = detectBackend();

/** "juce" when running inside the plugin's WebView, "mock" in a plain browser. */
export const bridgeMode = juce ? "juce" : "mock";

function emit(eventId, payload) {
  if (!juce) return false;
  try {
    juce.backend.emitEvent(eventId, payload);
    return true;
  } catch (err) {
    console.warn(`[petrichor] emitEvent("${eventId}") failed`, err);
    return false;
  }
}

function listen(eventId, fn) {
  if (!juce) return null;
  try {
    return juce.backend.addEventListener(eventId, (payload) => {
      try {
        fn(payload);
      } catch (err) {
        console.warn(`[petrichor] handler for "${eventId}" threw`, err);
      }
    });
  } catch (err) {
    console.warn(`[petrichor] addEventListener("${eventId}") failed`, err);
    return null;
  }
}

const relayNames = juce && Array.isArray(juce.init.__juce__sliders) ? juce.init.__juce__sliders : null;
const functionNames = juce && Array.isArray(juce.init.__juce__functions) ? juce.init.__juce__functions : null;

//==============================================================================
// Parameters

class ParamState {
  constructor(spec, connected) {
    this.spec = spec;
    this.id = spec.id;
    this.identifier = "__juce__slider" + spec.id;
    this.connected = connected; // a backend relay of this name exists
    this.range = defaultRange(spec);
    this.value = spec.def;
    this.version = 0;
    this.gestureDepth = 0;
    this.listeners = new Set();

    if (this.connected) {
      listen(this.identifier, (event) => this.handleEvent(event));
      emit(this.identifier, { eventType: "requestInitialUpdate" });
    }
  }

  // Arrow properties so they can be handed out unbound.
  subscribe = (fn) => {
    this.listeners.add(fn);
    return () => this.listeners.delete(fn);
  };

  getVersion = () => this.version;

  notify() {
    this.version++;
    for (const fn of this.listeners) fn();
  }

  get normalised() {
    return toNormalised(this.value, this.range);
  }

  setScaled = (scaled) => {
    const v = Number(scaled);
    if (!Number.isFinite(v)) return;
    const next = snapToInterval(v, this.range);
    if (next === this.value) return;
    this.value = next;
    this.notify();
    if (this.connected) emit(this.identifier, { eventType: "valueChanged", value: next });
  };

  setNormalised = (normalised) => {
    const n = Number(normalised);
    if (!Number.isFinite(n)) return;
    this.setScaled(fromNormalised(n, this.range));
  };

  dragStart = () => {
    if (++this.gestureDepth === 1 && this.connected) emit(this.identifier, { eventType: "sliderDragStarted" });
  };

  dragEnd = () => {
    if (this.gestureDepth === 0) return;
    if (--this.gestureDepth === 0 && this.connected) emit(this.identifier, { eventType: "sliderDragEnded" });
  };

  /** Resets to the default as one host gesture. */
  reset = () => {
    this.dragStart();
    this.setScaled(this.spec.def);
    this.dragEnd();
  };

  handleEvent(event) {
    if (!event || typeof event !== "object") return;

    if (event.eventType === "valueChanged") {
      const v = Number(event.value);
      // While the user holds the knob the backend only echoes what we sent; ignoring it avoids jitter.
      if (!Number.isFinite(v) || this.gestureDepth > 0 || v === this.value) return;
      this.value = v;
      this.notify();
    } else if (event.eventType === "propertiesChanged") {
      const start = Number(event.start);
      const end = Number(event.end);
      const skew = Number(event.skew);
      const interval = Number(event.interval);
      if (Number.isFinite(start) && Number.isFinite(end) && end > start) {
        this.range = {
          start,
          end,
          skew: Number.isFinite(skew) && skew > 0 ? skew : 1,
          interval: Number.isFinite(interval) && interval > 0 ? interval : 0,
        };
      }
      this.notify();
    }
  }
}

const paramStates = new Map();
const missingRelays = [];

for (const spec of PARAMS) {
  const connected = !!juce && (relayNames ? relayNames.includes(spec.id) : true);
  if (juce && !connected) missingRelays.push(spec.id);
  paramStates.set(spec.id, new ParamState(spec, connected));
}

if (missingRelays.length)
  console.warn(`[petrichor] backend has no slider relay for: ${missingRelays.join(", ")} (controls stay local)`);

export { missingRelays };

/** Returns the shared state object of a parameter (never throws). */
export function getParam(id) {
  let state = paramStates.get(id);
  if (!state) {
    console.warn(`[petrichor] unknown parameter "${id}"`);
    const spec = PARAM_BY_ID[id] || { id, name: id, short: id, unit: "", min: 0, max: 1, def: 0, centre: 0, desc: "" };
    state = new ParamState(spec, false);
    paramStates.set(id, state);
  }
  return state;
}

export function getParamValue(id) {
  return getParam(id).value;
}

/**
 * React binding of one parameter.
 * @returns {{ value: number, normalised: number, setNormalised: (n: number) => void,
 *            setScaled: (v: number) => void, dragStart: () => void, dragEnd: () => void,
 *            reset: () => void, spec: object, range: object, connected: boolean }}
 */
export function useParam(id) {
  const state = getParam(id);
  useSyncExternalStore(state.subscribe, state.getVersion, state.getVersion);
  return {
    value: state.value,
    normalised: state.normalised,
    setNormalised: state.setNormalised,
    setScaled: state.setScaled,
    dragStart: state.dragStart,
    dragEnd: state.dragEnd,
    reset: state.reset,
    spec: { ...state.spec, min: state.range.start, max: state.range.end },
    range: state.range,
    connected: state.connected || !juce,
  };
}

//==============================================================================
// Telemetry

const NUMERIC_FIELDS = [
  "windSpeed",
  "windGust",
  "gustFactor",
  "rainRate",
  "lambda",
  "grainRate",
  "meanDropMM",
  "impactSpeed",
  "activeVoices",
  "strikeCount",
  "lastStrikeDistance",
  "lastStrikeKey",
  "lastStrikeVelocity",
];

const KEY_COUNT = HIGHEST_KEY - LOWEST_KEY + 1;

function emptyTelemetry() {
  const t = { received: false, time: 0, keyLevels: new Float32Array(KEY_COUNT) };
  for (const k of NUMERIC_FIELDS) t[k] = 0;
  t.gustFactor = 1;
  return t;
}

let telemetry = emptyTelemetry();
const telemetryListeners = new Set();

function sanitizeTelemetry(raw, previous) {
  const t = { received: true, time: performance.now(), keyLevels: new Float32Array(KEY_COUNT) };
  const src = raw && typeof raw === "object" ? raw : {};
  for (const k of NUMERIC_FIELDS) {
    const v = Number(src[k]);
    t[k] = Number.isFinite(v) ? v : previous[k];
  }
  const levels = src.keyLevels;
  if (levels && typeof levels.length === "number") {
    const n = Math.min(KEY_COUNT, levels.length);
    for (let i = 0; i < n; i++) {
      const v = Number(levels[i]);
      t.keyLevels[i] = Number.isFinite(v) ? clamp(v, 0, 1.5) : 0;
    }
  }
  return t;
}

function publishTelemetry(raw) {
  telemetry = sanitizeTelemetry(raw, telemetry);
  for (const fn of telemetryListeners) {
    try {
      fn(telemetry);
    } catch (err) {
      console.warn("[petrichor] telemetry listener threw", err);
    }
  }
}

export function getTelemetry() {
  return telemetry;
}

/** Imperative subscription (visualiser, keyboard glow). Returns an unsubscribe function. */
export function subscribeTelemetry(fn) {
  telemetryListeners.add(fn);
  return () => telemetryListeners.delete(fn);
}

/** React hook: the latest telemetry, re-rendering at most `hz` times per second. */
export function useTelemetry(hz = 12) {
  const [snapshot, setSnapshot] = useState(getTelemetry);
  useEffect(() => {
    const minGap = 1000 / Math.max(1, hz);
    let lastUpdate = 0;
    let timer = null;
    const flush = () => {
      timer = null;
      lastUpdate = performance.now();
      setSnapshot(getTelemetry());
    };
    const unsubscribe = subscribeTelemetry(() => {
      const elapsed = performance.now() - lastUpdate;
      if (elapsed >= minGap) flush();
      else if (!timer) timer = setTimeout(flush, minGap - elapsed);
    });
    return () => {
      unsubscribe();
      if (timer) clearTimeout(timer);
    };
  }, [hz]);
  return snapshot;
}

//==============================================================================
// Native functions and notes

let nextResultId = 0;
const pendingCalls = new Map();
const warnedFunctions = new Set();

if (juce) {
  listen("__juce__complete", (event) => {
    const id = event && event.promiseId;
    const call = pendingCalls.get(id);
    if (!call) return;
    pendingCalls.delete(id);
    clearTimeout(call.timer);
    call.resolve(event.result);
  });
  listen("telemetry", publishTelemetry);
}

/** Calls a function registered with WebBrowserComponent::Options::withNativeFunction. */
export function callNative(name, ...params) {
  if (!juce) return Promise.resolve(undefined);
  if (functionNames && !functionNames.includes(name) && !warnedFunctions.has(name)) {
    warnedFunctions.add(name);
    console.warn(`[petrichor] native function "${name}" is unknown to the backend`);
  }
  const resultId = nextResultId++;
  return new Promise((resolve) => {
    // Never leak a pending promise if the backend does not answer.
    const timer = setTimeout(() => {
      pendingCalls.delete(resultId);
      resolve(undefined);
    }, 5000);
    pendingCalls.set(resultId, { resolve, timer });
    if (!emit("__juce__invoke", { name, params, resultId })) {
      clearTimeout(timer);
      pendingCalls.delete(resultId);
      resolve(undefined);
    }
  });
}

const mock = juce
  ? null
  : createMockBackend({ getValue: (id) => getParam(id).value, publish: publishTelemetry });

/** Note-on through the plugin's keyboard state (velocity 1..127). */
export function playNote(key, velocity) {
  const k = clamp(Math.round(key), 0, 127);
  const v = clamp(Math.round(velocity), 1, 127);
  if (mock) mock.noteOn(k, v);
  else callNative("noteOn", k, v);
}

export function releaseNote(key) {
  const k = clamp(Math.round(key), 0, 127);
  if (mock) mock.noteOff(k);
  else callNative("noteOff", k);
}
