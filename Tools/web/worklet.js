// Petrichor Piano in the browser: the engine (WebAssembly) inside an AudioWorklet.
// The same wrapper also runs on the main thread when a browser refuses the worklet.

function petrichorImports(module, getMemory) {
  const view = () => new DataView(getMemory().buffer);

  // The engine does no I/O; these few WASI calls only keep the C library content.
  const wasi = {
    fd_write(fd, iovs, iovsLength, written) {
      const dv = view();
      let total = 0;
      for (let i = 0; i < iovsLength; i++) total += dv.getUint32(iovs + 8 * i + 4, true);
      dv.setUint32(written, total, true);
      return 0;
    },
    random_get(buffer, length) {
      const bytes = new Uint8Array(getMemory().buffer, buffer, length);
      for (let i = 0; i < length; i++) bytes[i] = (Math.random() * 256) | 0;
      return 0;
    },
    environ_sizes_get(count, size) {
      const dv = view();
      dv.setUint32(count, 0, true);
      dv.setUint32(size, 0, true);
      return 0;
    },
    args_sizes_get(count, size) {
      const dv = view();
      dv.setUint32(count, 0, true);
      dv.setUint32(size, 0, true);
      return 0;
    },
    clock_time_get(id, precision, out) {
      view().setBigUint64(out, BigInt(Math.round(Date.now() * 1e6)), true);
      return 0;
    },
    proc_exit(code) {
      throw new Error("engine exited (" + code + ")");
    },
  };

  const imports = {};
  for (const entry of WebAssembly.Module.imports(module)) {
    if (entry.kind !== "function") continue;
    const space = imports[entry.module] || (imports[entry.module] = {});
    space[entry.name] = (entry.module.indexOf("wasi") === 0 && wasi[entry.name]) || (() => 0);
  }
  return imports;
}

/** Wraps an instantiated engine module: messages in, audio and telemetry out. */
function petrichorEngine(instance, rate) {
  const e = instance.exports;
  const memory = e.memory;
  if (typeof e._initialize === "function") e._initialize();
  e.pw_init(rate);

  const readString = (pointer) => {
    const bytes = new Uint8Array(memory.buffer, pointer);
    let s = "";
    for (let i = 0; bytes[i] !== 0; i++) s += String.fromCharCode(bytes[i]);
    return s;
  };
  const paramIndex = {};
  for (let i = 0, n = e.pw_param_count(); i < n; i++) paramIndex[readString(e.pw_param_id(i))] = i;

  return {
    handle(message) {
      switch (message.type) {
        case "param": {
          const index = paramIndex[message.id];
          if (index !== undefined) e.pw_set_param(index, message.value);
          break;
        }
        case "noteOn": e.pw_note_on(message.key, message.velocity); break;
        case "noteOff": e.pw_note_off(message.key); break;
        case "sustain": e.pw_sustain(message.down ? 1 : 0); break;
        case "soft": e.pw_soft(message.down ? 1 : 0); break;
        case "allNotesOff": e.pw_all_notes_off(); break;
      }
    },
    /** Renders up to 128 frames into left / right (Float32Arrays of equal length). */
    render(left, right) {
      const n = Math.min(left.length, 128);
      e.pw_process(n);
      left.set(new Float32Array(memory.buffer, e.pw_left(), n));
      if (right) right.set(new Float32Array(memory.buffer, e.pw_right(), n));
    },
    telemetry() {
      return new Float32Array(memory.buffer, e.pw_telemetry(), 13 + 88).slice();
    },
  };
}

/** Main-thread fallback: compiles asynchronously (Chrome refuses large synchronous compiles there). */
async function petrichorEngineAsync(wasmBytes, rate) {
  const module = await WebAssembly.compile(wasmBytes);
  let instance = null;
  const imports = petrichorImports(module, () => instance.exports.memory);
  instance = await WebAssembly.instantiate(module, imports);
  return petrichorEngine(instance, rate);
}

if (typeof AudioWorkletProcessor !== "undefined") {
  class PetrichorProcessor extends AudioWorkletProcessor {
    constructor(options) {
      super();
      this.engine = null;
      this.sinceTelemetry = 0;
      try {
        const module = new WebAssembly.Module(options.processorOptions.wasm);
        let instance = null;
        const imports = petrichorImports(module, () => instance.exports.memory);
        instance = new WebAssembly.Instance(module, imports);
        this.engine = petrichorEngine(instance, sampleRate);
        this.port.postMessage({ type: "ready" });
      } catch (err) {
        this.port.postMessage({ type: "error", message: String((err && err.message) || err) });
      }
      this.port.onmessage = (event) => {
        if (this.engine) this.engine.handle(event.data);
      };
    }

    process(inputs, outputs) {
      const out = outputs[0];
      if (!this.engine || !out || !out[0]) return true;
      this.engine.render(out[0], out[1]);
      this.sinceTelemetry += out[0].length;
      if (this.sinceTelemetry >= sampleRate / 30) {
        this.sinceTelemetry = 0;
        this.port.postMessage({ type: "telemetry", values: this.engine.telemetry() });
      }
      return true;
    }
  }
  registerProcessor("petrichor", PetrichorProcessor);
}
