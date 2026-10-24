// SPDX-License-Identifier: GPL-3.0-only
// Demake's AudioWorklet: the firmware's core in one WebAssembly instance, as it runs on the FM-1. The page sends the
// controls; this sends back the screen (4 bits a pixel), its 16 colours and the LEDs, 30 times a second, and the
// store after each save (the page keeps it: the songs and sounds saved in the browser).
class Demake extends AudioWorkletProcessor {
  constructor() {
    super();
    this.x = null;
    this.lastFrame = -1e9;
    this.port.onmessage = (ev) => this.msg(ev.data);
  }

  msg(m) {
    if (m.t === "wasm") {
      const inst = new WebAssembly.Instance(new WebAssembly.Module(m.bytes), {});
      this.x = inst.exports;
      if (this.x._initialize) this.x._initialize();
      if (m.store && m.store.length === 65536) new Uint8Array(this.x.memory.buffer, this.x.w_store(), 65536).set(m.store);
      this.x.w_init(m.seed >>> 0);
      this.storeGen = this.x.w_store_gen();
      this.audio = new Int16Array(this.x.memory.buffer, this.x.w_audio(), 256);
      this.port.postMessage({ t: "ready" });
      return;
    }
    const x = this.x;
    if (!x) return;
    switch (m.t) {
      case "key": x.w_key(m.k, m.d); break;
      case "btn": x.w_button(m.b, m.d); break;
      case "enc": x.w_encoder(m.e, m.d); break;
      case "midi": x.w_midi(m.s, m.a, m.b); break;
    }
  }

  process(inputs, outputs) {
    const x = this.x, o = outputs[0];
    if (!x) return true;
    x.w_render();
    const a = this.audio;
    for (let i = 0; i < 128; i++) {
      o[0][i] = a[i * 2] / 32768;
      if (o[1]) o[1][i] = a[i * 2 + 1] / 32768;
    }
    if (x.w_store_gen() !== this.storeGen) {            /* a save: the store to the page, which keeps it */
      this.storeGen = x.w_store_gen();
      const st = new Uint8Array(x.memory.buffer, x.w_store(), 65536).slice();
      this.port.postMessage({ t: "store", st }, [st.buffer]);
    }
    const ms = Math.floor(currentTime * 1000);
    if (ms - this.lastFrame >= 33) {
      this.lastFrame = ms;
      const mem = x.memory.buffer;
      const fb = new Uint8Array(mem, x.w_frame(ms), 28800).slice();
      const pal = new Uint32Array(mem, x.w_palette(), 16).slice();
      const leds = new Uint8Array(mem, x.w_leds(), 41).slice();
      this.port.postMessage({ t: "frame", fb, pal, leds }, [fb.buffer, pal.buffer, leds.buffer]);
    }
    return true;
  }
}
registerProcessor("demake", Demake);
