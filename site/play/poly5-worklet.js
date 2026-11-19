/* SPDX-License-Identifier: GPL-3.0-only */
/* POLY5 in the browser's audio thread: the firmware (poly5.wasm, made by dinghy/web/build.sh) runs here, a little at
   a time, as the sound card asks for sound. So the time it keeps is the sound's time, as on the FM-1. Its screen,
   lights and saved patches go to the page (poly5-player.js); the page's keys, buttons and knobs come in. */
class Poly5Processor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const { module, store } = options.processorOptions;
    this.w = new WebAssembly.Instance(module, {}).exports;
    const w = this.w;
    if (store && store.byteLength === w.web_store_size()) {          // the patches you saved last time
      new Uint8Array(w.memory.buffer, w.web_store(), w.web_store_size()).set(new Uint8Array(store));
      w.web_store_in();
    }
    this.screenGen = -1; this.lastShot = 0; this.leds = -1; this.storeGen = w.web_store_gen(); this.on = true;
    this.port.onmessage = (e) => {
      const m = e.data;
      if (m.t === 'keys') w.web_keys(m.mask >>> 0);
      else if (m.t === 'btn') w.web_button(m.id, m.down ? 1 : 0);
      else if (m.t === 'turn') w.web_turn(m.enc, m.n);
      else if (m.t === 'midi') w.web_midi(m.st, m.d1, m.d2);
      else if (m.t === 'vol') w.web_volume(m.g);
      else if (m.t === 'off') this.on = false;
    };
  }
  process(inputs, outputs) {
    const out = outputs[0], L = out[0], R = out[1] || out[0], n = L.length, w = this.w;
    w.web_fill(n);                                                     // the FM-1 a little further
    const size = w.web_audio_size(), r = w.web_audio_read();
    const f = new Float32Array(w.memory.buffer, w.web_audio(), size * 2);
    for (let i = 0; i < n; i++) { const k = ((r + i) % size) * 2; L[i] = f[k]; R[i] = f[k + 1]; }
    w.web_audio_take(n);
    const ms = w.web_ms();
    if (ms - this.lastShot >= 33) {                                   // the screen and the lights, 30 times a second
      this.lastShot = ms;
      const g = w.web_screen_gen(), leds = w.web_leds_lo(), hi = w.web_leds_hi(), glow = w.web_glow_lo();
      if (g !== this.screenGen) {
        this.screenGen = g;
        const px = new Uint16Array(w.memory.buffer, w.web_screen(), 240 * 240).slice();
        this.port.postMessage({ t: 'frame', px, leds, hi, glow }, [px.buffer]);
      } else if (leds !== this.leds) this.port.postMessage({ t: 'leds', leds, hi, glow });
      this.leds = leds;
      const sg = w.web_store_gen();
      if (sg !== this.storeGen) {                                     // a patch saved: the page keeps it
        this.storeGen = sg;
        w.web_store_out();
        const s = new Uint8Array(w.memory.buffer, w.web_store(), w.web_store_size()).slice();
        this.port.postMessage({ t: 'store', s }, [s.buffer]);
      }
    }
    return this.on;
  }
}
registerProcessor('poly5', Poly5Processor);
