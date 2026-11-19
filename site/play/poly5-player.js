/* SPDX-License-Identifier: GPL-3.0-only */
/* POLY5 in the browser: a playable FM-1. The panel is drawn as the FM-1 is laid out (as manual.FM1 draws it for the
   videos); the firmware itself runs in the audio thread (poly5-worklet.js, poly5.wasm). Mouse, touch, the computer's
   keys and a MIDI keyboard play it. Your saved patches stay in this browser (localStorage).
     new Poly5Player(element)   (element: data-wasm, data-worklet: their URLs)                                                              */
(function () {
  const NS = 'http://www.w3.org/2000/svg';
  const COL = ['#6aa8f0', '#f08a70', '#7dd498', '#f0c870', '#d890a8'], AMBER = '#f0a030';
  const KNOBS = [ // name, x, y, encoder (null: MASTER, the volume), what it does on the home screen
    ['MASTER', 155, 165, null, 'VOLUME'], ['SELECT', 340, 165, 0, 'CURSOR'], ['PRESETS', 155, 348, 6, 'PATCHES'],
    ['ALGORITHM', 340, 348, 1, 'PAGES'], ['KNOB1', 990, 165, 2, 'FILTER'], ['KNOB2', 1185, 165, 3, 'RESONANCE'],
    ['KNOB3', 1380, 165, 4, 'DELAY'], ['KNOB4', 1575, 165, 5, 'REVERB']];
  const ROWS = [['FX', 'SEL', 'ENV', 'LFO', 'EDIT', 'GLO'], ['HOME', 'SAVE', 'ARP', 'SEQ', 'PLAY', 'REC']];
  const BTN = { 'OCT-': 0, 'OCT+': 1, FX: 2, SEL: 3, ENV: 4, LFO: 5, EDIT: 6, GLO: 7, HOME: 8, SAVE: 9, ARP: 10, SEQ: 11, PLAY: 12, REC: 13 };
  const SUB = { GLO: 'RANDOM', PLAY: 'START', HOME: 'BACK' };
  // the computer's keys: A is C4 (FM-1 key 7: key 0 is F3)
  const NOTE_KEYS = { a: 7, w: 8, s: 9, e: 10, d: 11, f: 12, t: 13, g: 14, y: 15, h: 16, u: 17, j: 18, k: 19, o: 20, l: 21, p: 22, ';': 23, "'": 24 };
  const KEY_BTN = { z: 'OCT-', x: 'OCT+', ' ': 'PLAY', 1: 'FX', 2: 'SEL', 3: 'ENV', 4: 'LFO', 5: 'EDIT', r: 'GLO', q: 'ARP', b: 'HOME' };
  const STORE = 'poly5-store-v1';

  function el(name, attrs, parent) {
    const e = document.createElementNS(NS, name);
    for (const k in attrs) e.setAttribute(k, attrs[k]);
    if (parent) parent.appendChild(e);
    return e;
  }
  function text(parent, x, y, s, size, fill, weight, family) {
    const t = el('text', { x, y, 'font-size': size, fill, 'text-anchor': 'middle', 'font-weight': weight || 600,
      'font-family': family || '"Space Grotesk", "Helvetica Neue", Arial, sans-serif', 'pointer-events': 'none' }, parent);
    t.textContent = s;
    return t;
  }
  function dim(hex, k) {
    const n = parseInt(hex.slice(1), 16);
    return `rgb(${(n >> 16) * k | 0},${(n >> 8 & 255) * k | 0},${(n & 255) * k | 0})`;
  }
  function b64(u8) { let s = ''; for (let i = 0; i < u8.length; i += 0x8000) s += String.fromCharCode.apply(null, u8.subarray(i, i + 0x8000)); return btoa(s); }
  function unb64(s) { const b = atob(s), u = new Uint8Array(b.length); for (let i = 0; i < b.length; i++) u[i] = b.charCodeAt(i); return u; }

  class Poly5Player {
    constructor(root, opts) {
      this.root = root; this.wasmUrl = root.dataset.wasm; this.workletUrl = root.dataset.worklet;
      this.keyMask = 0; this.ptrKeys = new Map(); this.held = new Set(); this.pos = [0, 0, 0, 0, 0, 0, 0];
      this.vol = 0.8; this.node = null; this.ctx = null;
      this.build();
    }

    build() {
      const wrap = document.createElement('div');
      wrap.className = 'p5-device';
      const svg = el('svg', { viewBox: '0 0 1760 1072', class: 'p5-svg', role: 'application',
        'aria-label': 'A playable FM-1 running POLY5: knobs, buttons and keys' });
      wrap.appendChild(svg);
      this.root.appendChild(wrap);
      this.svg = svg;
      el('rect', { x: 8, y: 8, width: 1744, height: 1056, rx: 64, fill: '#1e1f25', stroke: '#30323a', 'stroke-width': 3 }, svg);
      // knobs
      this.knobs = [];
      for (const [name, x, y, enc, does] of KNOBS) {
        text(svg, x, (y < 300 ? 78 : 262) + 14, name, 19, '#60646f', 400, 'Menlo, monospace');
        const g = el('g', { class: 'p5-knob', tabindex: 0, role: 'slider', 'aria-label': name }, svg);
        el('circle', { cx: x, cy: y, r: 50, fill: 'transparent' }, g);
        el('circle', { cx: x, cy: y, r: 46, fill: '#121317', stroke: '#363840', 'stroke-width': 4, 'stroke-dasharray': '2 6' }, g);
        el('circle', { cx: x, cy: y, r: 34, fill: '#2c2e36', stroke: '#3c3e48', 'stroke-width': 1.5 }, g);
        const i = KNOBS.findIndex(k => k[0] === name);
        const mark = el('line', { x1: x, y1: y - 8, x2: x, y2: y - 28, stroke: i >= 4 ? COL[i - 4] : '#ece8e0', 'stroke-width': 5, 'stroke-linecap': 'round' }, g);
        text(svg, x, y + 80, does, 22, i >= 4 ? COL[i - 4] : '#ece8e0');
        const k = { name, x, y, enc, g, mark, angle: name === 'MASTER' ? 60 : 0 };
        this.knobs.push(k);
        this.knobEvents(k);
        this.drawKnob(k);
      }
      // OCT- / OCT+
      el('rect', { x: 118, y: 455, width: 258, height: 73, rx: 14, fill: '#16171c' }, svg);
      this.btns = {};
      const mkBtn = (name, x, y, hw, hh) => {
        const g = el('g', { class: 'p5-btn', tabindex: 0, role: 'button', 'aria-label': name }, svg);
        const r = el('rect', { x: x - hw, y: y - hh, width: hw * 2, height: hh * 2, rx: 9 }, g);
        const si = ['FX', 'SEL', 'ENV', 'LFO', 'EDIT'].indexOf(name);
        const t1 = text(g, x, y + (si >= 0 || SUB[name] ? -6 : 7), name.replace('-', '−'), 20, '#fff', 700);
        const t2 = si >= 0 ? text(g, x, y + 20, String(si + 1), 22, '#fff', 700) : SUB[name] ? text(g, x, y + 20, SUB[name], 11, '#fff', 400, 'Menlo, monospace') : null;
        const b = { name, id: BTN[name], g, r, t1, t2, si, lit: false };
        this.btns[name] = b;
        const down = (e) => { e.preventDefault(); g.setPointerCapture && e.pointerId !== undefined && g.setPointerCapture(e.pointerId); this.button(name, true); };
        const up = () => this.button(name, false);
        g.addEventListener('pointerdown', down);
        g.addEventListener('pointerup', up); g.addEventListener('pointercancel', up);
        g.addEventListener('keydown', (e) => { if ((e.key === 'Enter' || e.key === ' ') && !e.repeat) { e.preventDefault(); e.stopPropagation(); this.button(name, true); } });
        g.addEventListener('keyup', (e) => { if (e.key === 'Enter' || e.key === ' ') { e.stopPropagation(); this.button(name, false); } });
        this.drawBtn(b);
      };
      mkBtn('OCT-', 181, 491, 42, 21); mkBtn('OCT+', 314, 491, 42, 21);
      // the screen (a canvas over the drawing)
      el('rect', { x: 462, y: 82, width: 410, height: 410, rx: 30, fill: '#000', stroke: '#0e0e10', 'stroke-width': 6 }, svg);
      const cv = document.createElement('canvas');
      cv.width = 240; cv.height = 240; cv.className = 'p5-screen';
      wrap.appendChild(cv);
      this.cv = cv; this.c2 = cv.getContext('2d'); this.img = this.c2.createImageData(240, 240);
      // the button block
      el('rect', { x: 944, y: 292, width: 682, height: 250, rx: 18, fill: '#16171c' }, svg);
      ROWS.forEach((row, r) => row.forEach((name, i) => mkBtn(name, 1012 + i * 108.4, 357 + r * 109, 36, 36)));
      // the keys: pills with a light slit; 16 below, 11 above
      el('rect', { x: 68, y: 584, width: 1624, height: 398, rx: 40, fill: '#16171c' }, svg);
      const low = Array.from({ length: 16 }, (_, i) => 149 + i * 97.6);
      const up = [0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14].map(a => (low[a] + low[a + 1]) / 2);
      const whites = [], blacks = [];
      for (let k = 0; k < 27; k++) ([0, 2, 4, 5, 7, 9, 11].includes((k + 53) % 12) ? whites : blacks).push(k);
      this.keys = [];
      const mkKey = (k, x, y0) => {
        const g = el('g', { class: 'p5-key', 'data-key': k }, svg);
        const body = el('rect', { x: x - 42, y: y0, width: 84, height: 150, rx: 42, fill: '#2e3038' }, g);
        const slit = el('rect', { x: x - 8, y: y0 + 24, width: 16, height: 62, rx: 8, fill: '#70727a' }, g);
        this.keys[k] = { g, body, slit };
      };
      whites.forEach((k, i) => mkKey(k, low[i], 792));
      blacks.forEach((k, i) => mkKey(k, up[i], 626));
      text(svg, low[4], 922, 'C4', 17, '#55585f', 400, 'Menlo, monospace');
      text(svg, low[11], 922, 'C5', 17, '#55585f', 400, 'Menlo, monospace');
      this.keyEvents();
      // switch it on (sound needs a press first)
      const on = document.createElement('button');
      on.type = 'button'; on.className = 'p5-on';
      on.innerHTML = '<span class="p5-on-dot"></span><b>Switch on</b><small>sound on, then play</small>';
      on.addEventListener('click', () => this.start());
      wrap.appendChild(on);
      this.onBtn = on;
      this.wrap = wrap;
    }

    drawKnob(k) { k.mark.setAttribute('transform', `rotate(${k.angle} ${k.x} ${k.y})`); }
    knobEvents(k) {
      let lastY = null, acc = 0;
      const turn = (n) => {
        if (!n) return;
        k.angle += n * 18;
        if (k.enc === null) { k.angle = Math.max(-135, Math.min(135, k.angle)); this.vol = (k.angle + 135) / 270 * 1.4; this.send({ t: 'vol', g: this.vol }); }
        else this.send({ t: 'turn', enc: k.enc, n });
        this.drawKnob(k);
        k.g.classList.add('on'); clearTimeout(k.tm); k.tm = setTimeout(() => k.g.classList.remove('on'), 250);
      };
      k.g.addEventListener('pointerdown', (e) => { e.preventDefault(); k.g.setPointerCapture(e.pointerId); lastY = e.clientY; acc = 0; });
      k.g.addEventListener('pointermove', (e) => {
        if (lastY === null) return;
        acc += lastY - e.clientY + (e.movementX || 0) * 0;
        lastY = e.clientY;
        const n = Math.trunc(acc / 10); acc -= n * 10; turn(n);              // up = clockwise, 10 px a detent
      });
      const end = () => { lastY = null; };
      k.g.addEventListener('pointerup', end); k.g.addEventListener('pointercancel', end);
      k.g.addEventListener('wheel', (e) => { e.preventDefault(); turn(e.deltaY < 0 ? 1 : -1); }, { passive: false });
      k.g.addEventListener('keydown', (e) => {
        if (e.key === 'ArrowUp' || e.key === 'ArrowRight') { e.preventDefault(); e.stopPropagation(); turn(1); }
        if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') { e.preventDefault(); e.stopPropagation(); turn(-1); }
      });
    }
    keyEvents() {
      const keyAt = (e) => { const t = document.elementFromPoint(e.clientX, e.clientY); const g = t && t.closest && t.closest('.p5-key'); return g ? +g.dataset.key : -1; };
      this.svg.addEventListener('pointerdown', (e) => {
        const k = keyAt(e); if (k < 0) return;
        e.preventDefault(); this.svg.setPointerCapture(e.pointerId);
        this.ptrKeys.set(e.pointerId, k); this.sendKeys();
      });
      this.svg.addEventListener('pointermove', (e) => {
        if (!this.ptrKeys.has(e.pointerId)) return;
        const k = keyAt(e);
        if (k >= 0 && k !== this.ptrKeys.get(e.pointerId)) { this.ptrKeys.set(e.pointerId, k); this.sendKeys(); }   // a glissando
      });
      const up = (e) => { if (this.ptrKeys.delete(e.pointerId)) this.sendKeys(); };
      this.svg.addEventListener('pointerup', up); this.svg.addEventListener('pointercancel', up);
      this.onKeyDown = (e) => {
        if (!this.node || e.metaKey || e.ctrlKey || e.altKey) return;
        const tag = (e.target && e.target.tagName) || '';
        if (tag === 'INPUT' || tag === 'TEXTAREA') return;
        const key = e.key.toLowerCase();
        if (key in NOTE_KEYS) { e.preventDefault(); if (!e.repeat) { this.held.add(NOTE_KEYS[key]); this.sendKeys(); } }
        else if (key in KEY_BTN && !e.repeat && !(e.target && e.target.closest && e.target.closest('.p5-btn,.p5-knob'))) { e.preventDefault(); this.button(KEY_BTN[key], true); }
      };
      this.onKeyUp = (e) => {
        const key = e.key.toLowerCase();
        if (key in NOTE_KEYS) { this.held.delete(NOTE_KEYS[key]); this.sendKeys(); }
        else if (key in KEY_BTN) this.button(KEY_BTN[key], false);
      };
    }
    sendKeys() {
      let m = 0;
      for (const k of this.held) m |= 1 << k;
      for (const k of this.ptrKeys.values()) m |= 1 << k;
      this.keyMask = m >>> 0;
      this.send({ t: 'keys', mask: this.keyMask });
      this.drawKeys();
    }
    button(name, down) {
      const b = this.btns[name]; if (!b) return;
      if (b.down === down) return;
      b.down = down; this.drawBtn(b);
      this.send({ t: 'btn', id: b.id, down });
    }
    send(m) { if (this.node) this.node.port.postMessage(m); }

    drawBtn(b) {
      let fill, ink;
      if (b.si >= 0) { fill = b.lit ? COL[b.si] : dim(COL[b.si], b.glow ? 0.42 : 0.24); ink = b.lit ? '#101014' : dim(COL[b.si], 0.95); }
      else if (b.name === 'GLO') { fill = b.lit ? AMBER : '#76501e'; ink = b.lit ? '#181006' : '#fad6a0'; }
      else if (b.name === 'REC') { fill = b.lit ? '#ec4038' : '#6e2220'; ink = '#f0e0dc'; }
      else { fill = b.lit ? '#ece4d2' : '#50525c'; ink = b.lit ? '#181614' : '#ece8e0'; }
      b.r.setAttribute('fill', fill);
      b.r.setAttribute('stroke', b.down ? AMBER : 'none'); b.r.setAttribute('stroke-width', b.down ? 6 : 0);
      b.t1.setAttribute('fill', ink); if (b.t2) b.t2.setAttribute('fill', ink);
    }
    drawKeys() {
      for (let k = 0; k < 27; k++) {
        const key = this.keys[k], on = (this.keyMask >>> k) & 1, led = this.keyLeds ? (this.keyLeds >>> k) & 1 : 0;
        key.slit.setAttribute('fill', on ? AMBER : led ? '#fae2aa' : '#70727a');
        key.body.setAttribute('fill', on ? '#3a3220' : '#2e3038');
      }
    }
    lights(leds, hi, glow) {
      for (const name in this.btns) {
        const b = this.btns[name], lit = !!((leds >>> b.id) & 1), gl = !!((glow >>> b.id) & 1);
        if (lit !== b.lit || gl !== b.glow) { b.lit = lit; b.glow = gl; this.drawBtn(b); }
      }
      const kl = ((leds >>> 14) | (hi << 18)) >>> 0;              // the keys' lights: ids 14..40
      if (kl !== this.keyLeds) { this.keyLeds = kl; this.drawKeys(); }
    }
    frame(px) {
      const d = this.img.data;
      for (let i = 0; i < 240 * 240; i++) {
        const c = px[i];
        d[i * 4] = (c >> 11) * 255 / 31; d[i * 4 + 1] = (c >> 5 & 63) * 255 / 63; d[i * 4 + 2] = (c & 31) * 255 / 31; d[i * 4 + 3] = 255;
      }
      this.c2.putImageData(this.img, 0, 0);
    }

    async start() {
      if (this.node) return;
      this.onBtn.classList.add('busy');
      try {
        const AC = window.AudioContext || window.webkitAudioContext;
        let ctx;
        try { ctx = new AC({ sampleRate: 44100, latencyHint: 'interactive' }); } catch (e) { ctx = new AC({ latencyHint: 'interactive' }); }
        this.ctx = ctx;
        await ctx.audioWorklet.addModule(this.workletUrl);
        const res = await fetch(this.wasmUrl);
        const module = await WebAssembly.compile(await res.arrayBuffer());
        let store = null;
        try { const s = localStorage.getItem(STORE); if (s) store = unb64(s).buffer; } catch (e) {}
        const node = new AudioWorkletNode(ctx, 'poly5', { numberOfInputs: 0, outputChannelCount: [2], processorOptions: { module, store } });
        node.port.onmessage = (e) => {
          const m = e.data;
          if (m.t === 'frame') { this.frame(m.px); this.lights(m.leds, m.hi, m.glow); }
          else if (m.t === 'leds') this.lights(m.leds, m.hi, m.glow);
          else if (m.t === 'store') { try { localStorage.setItem(STORE, b64(m.s)); } catch (err) {} }
        };
        node.connect(ctx.destination);
        await ctx.resume();
        this.node = node;
        node.port.postMessage({ t: 'vol', g: this.vol });
        window.addEventListener('keydown', this.onKeyDown);
        window.addEventListener('keyup', this.onKeyUp);
        window.addEventListener('blur', () => { this.held.clear(); this.ptrKeys.clear(); this.sendKeys(); });
        this.wrap.classList.add('running');
        this.midi();
      } catch (err) {
        this.onBtn.classList.remove('busy');
        this.onBtn.querySelector('small').textContent = 'This browser could not start it: ' + (err && err.message || err);
      }
    }
    async midi() {
      if (!navigator.requestMIDIAccess) return;
      try {
        const acc = await navigator.requestMIDIAccess();
        const hook = (inp) => { inp.onmidimessage = (e) => { const d = e.data; if (d[0] >= 0xF0 && d[0] !== 0xF8 && d[0] !== 0xFA && d[0] !== 0xFC) return; this.send({ t: 'midi', st: d[0], d1: d[1] || 0, d2: d[2] || 0 }); }; };
        acc.inputs.forEach(hook);
        acc.onstatechange = (e) => { if (e.port.type === 'input' && e.port.state === 'connected') hook(e.port); };
      } catch (e) { /* no MIDI: the panel and the keys still play */ }
    }
  }
  window.Poly5Player = Poly5Player;
})();
