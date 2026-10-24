// SPDX-License-Identifier: GPL-3.0-only
// Demake's emulator page: the FM-1's panel as line art (its geometry as the community's emulators measured it, as
// MONO2K's panel.js has it), the screen from the worklet, and the controls from mouse, touch and the computer's keys.
// A monotone panel in the screen's colours, and what is lit in the screen's thermal light, 8-bit: a sprite of the lit
// shape over each key and button, lit from its edge in by the chessboard depth the screen uses.
(() => {
  "use strict";
  const NS = "http://www.w3.org/2000/svg";
  const PAPER = "#f5f5f2", INK = "#18181a", LABEL = "#8e8e8a";
  const WHITE_X = [71, 119, 167, 215, 264, 312, 360, 408, 456, 504, 553, 601, 649, 697, 745, 794];
  const BLACK_X = [95, 143, 191, 288, 336, 432, 480, 529, 625, 673, 769];
  const isBlack = (k) => (0x54a >> ((k + 5) % 12)) & 1;
  const KEYR = [];
  for (let k = 0, w = 0, b = 0; k < 27; k++) KEYR.push(isBlack(k) ? [BLACK_X[b++], 330, 38, 70] : [WHITE_X[w++], 412, 44, 112]);
  const BTNR = [];
  for (let i = 0; i < 12; i++) BTNR.push([[500, 554, 608, 661, 715, 768][i % 6], [177, 238][(i / 6) | 0], 40, 34]);
  BTNR.push([87, 256, 48, 26], [153, 256, 48, 26]);
  const BTN = ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT−", "OCT+"];
  const ROLE = ["FX", "Filter", "Amp", "LFO", "Sound", "Global", "Home", "Songs", "Chord", "Roll", "", "", "", ""];
  const ENC = ["Select", "Algorithm", "Presets", "K1", "K2", "K3", "K4", "Master"];
  const EROLE = ["Synth", "", "Sound", "", "", "", "", "Volume"];
  const ENC_CX = [186.5, 186.5, 95, 508, 604, 701, 797, 95], ENC_CY = [101, 191, 191, 101, 101, 101, 101, 101];
  const LCD = [247, 59, 201];
  const VB = [14, 14, 876, 538];

  const svg = document.getElementById("svg"), lcd = document.getElementById("lcd"), panel = document.getElementById("panel");
  if (!panel) return;                                  /* a page without the emulator (the installer) */
  const lg = lcd.getContext("2d"), img = lg.createImageData(240, 240);
  const el = (t, a, p) => { const e = document.createElementNS(NS, t); for (const k in a) e.setAttribute(k, a[k]); (p || svg).appendChild(e); return e; };
  const label = (x, y, s, size = 11, fill = INK) => { const t = el("text", { x, y, "text-anchor": "middle", fill, "font-size": size, "font-family": "geist, sans-serif", "font-weight": 500 }); t.textContent = s; return t; };

  // ---- the panel: one ink line, rounded; what is lit glows from its edge in, 8-bit, as the screen draws it: a sprite
  // over the key or the button (tools/gen_art.py), its bits stepping through the six arrangements
  const lits = [];
  const litImg = (x, y, w, h, shape) => {
    const im = el("image", { x, y, width: w, height: h, href: `img/bits/${shape}_0.png`, preserveAspectRatio: "none", "pointer-events": "none" });
    im.style.imageRendering = "pixelated"; im.style.display = "none"; im.dataset.shape = shape;
    for (let k = 0; k < 6; k++) new Image().src = `img/bits/${shape}_${k}.png`;
    lits.push(im);
    return im;
  };
  const light = (e, on, fill) => { e.setAttribute("fill", fill); e.img.style.display = on ? "" : "none"; };
  setInterval(() => {
    for (const im of lits) if (im.style.display !== "none" && Math.random() < 0.5) im.setAttribute("href", `img/bits/${im.dataset.shape}_${(Math.random() * 6) | 0}.png`);
  }, 140);
  el("rect", { x: 20, y: 20, width: 864, height: 526, rx: 44, fill: PAPER, stroke: INK, "stroke-width": 2 });
  el("rect", { x: LCD[0] - 9, y: LCD[1] - 9, width: LCD[2] + 18, height: LCD[2] + 18, rx: 16, fill: INK });
  const keyEls = KEYR.map(([x, y, w, h], k) => {
    const b = isBlack(k);
    const r = el("rect", { x: x - w / 2, y, width: w, height: h, rx: 10, fill: b ? INK : PAPER, stroke: INK, "stroke-width": 1.5 });
    r.dataset.k = k;
    r.img = litImg(x - w / 2, y, w, h, b ? "keyb" : "keyw");
    return r;
  });
  el("line", { x1: 46, y1: 316, x2: 858, y2: 316, stroke: INK, "stroke-width": 1 });
  const btnEls = BTNR.map(([x, y, w, h], i) => {
    const r = el("rect", { x: x - w / 2, y: y - h / 2, width: w, height: h, rx: 12, fill: PAPER, stroke: INK, "stroke-width": 1.5 });
    r.dataset.b = i;
    r.img = litImg(x - w / 2, y - h / 2, w, h, i < 12 ? "btn" : "oct");
    const t = label(x, y + 4, BTN[i]);
    t.setAttribute("pointer-events", "none");
    if (ROLE[i]) label(x, y + h / 2 + 13, ROLE[i], 10, LABEL);
    return { r, t };
  });
  const encEls = ENC_CX.map((cx, e) => {
    const big = e >= 3 && e <= 6, r = big ? 24 : 20, cy = ENC_CY[e];
    const g = el("g", {});
    el("circle", { cx, cy, r, fill: PAPER, stroke: INK, "stroke-width": 1.5 }, g);
    const mark = el("line", { x1: cx, y1: cy - r + 4, x2: cx, y2: cy - r + 12, stroke: INK, "stroke-width": 2.5, "stroke-linecap": "round" }, g);
    g.dataset.e = e;
    label(cx, cy - r - 8, ENC[e]);
    if (EROLE[e]) label(cx, cy + r + 15, EROLE[e], 10, LABEL);
    return { g, mark, cx, cy, angle: 0 };
  });

  // the screen sits over the LCD, wherever the panel is drawn
  function place() {
    const s = svg.getBoundingClientRect(), k = s.width / VB[2];
    lcd.style.left = (LCD[0] - VB[0]) * k + "px";
    lcd.style.top = (LCD[1] - VB[1]) * k + "px";
    lcd.style.width = lcd.style.height = LCD[2] * k + "px";
  }
  new ResizeObserver(place).observe(panel);
  place();
  lg.fillStyle = PAPER; lg.fillRect(0, 0, 240, 240);
  {                                                     /* before the core runs: the wordmark, as the boot draws it */
    const wm = new Image();
    wm.onload = () => document.fonts.load("500 12px geist").then(() => {
      if (node) return;
      lg.imageSmoothingEnabled = false;
      lg.drawImage(wm, 0, 0, 384, 96, 24, 96, 192, 48);   /* the sprite's first frame */
      lg.fillStyle = LABEL; lg.font = "500 12px geist, sans-serif"; lg.textAlign = "center"; lg.fillText("Power on to start", 120, 170);
    });
    wm.src = "img/wordmark.png";
  }

  // ---- the store: the songs and sounds saved in the emulator, kept in this browser (64 KiB, as on the FM-1)
  const STORE_KEY = "demake-store-1";
  function storeLoad() {
    try {
      const t = localStorage.getItem(STORE_KEY);
      if (!t) return null;
      const b = atob(t), u = new Uint8Array(b.length);
      for (let i = 0; i < b.length; i++) u[i] = b.charCodeAt(i);
      return u.length === 65536 ? u : null;
    } catch (e) { return null; }
  }
  function storeKeep(u) {
    try {
      let b = "";
      for (let i = 0; i < u.length; i += 8192) b += String.fromCharCode.apply(null, u.subarray(i, i + 8192));
      localStorage.setItem(STORE_KEY, btoa(b));
    } catch (e) { /* storage off or full: the save lasts until the page closes */ }
  }

  // ---- the sound: the core in a worklet
  let ctx = null, node = null, gain = null, master = 0.8;
  const send = (m) => { if (node) node.port.postMessage(m); };
  const power = document.getElementById("power");
  power.addEventListener("click", async () => {
    if (ctx) { ctx.resume(); return; }
    try {
      ctx = new AudioContext({ sampleRate: 44100 });
      await ctx.audioWorklet.addModule("worklet.js?v=11");
      node = new AudioWorkletNode(ctx, "demake", { outputChannelCount: [2] });
      gain = ctx.createGain(); gain.gain.value = master;
      node.connect(gain).connect(ctx.destination);
      const bytes = await (await fetch("dmk.wasm?v=11")).arrayBuffer();
      node.port.onmessage = (ev) => onmsg(ev.data);
      node.port.postMessage({ t: "wasm", bytes, seed: (Math.random() * 4294967296) >>> 0, store: storeLoad() });
      power.textContent = "On";
      midiOn();
    } catch (e) { power.textContent = "No sound: " + e.message; }
  });

  // the volume: the screen's meter, ticks ink up to the value, its own tick tall and lit
  const vol = document.getElementById("vol"), TICKS = 41, tick = [];
  for (let i = 0; i < TICKS; i++) tick.push(vol.appendChild(document.createElement("i")));
  const setVol = (v) => {
    master = Math.max(0, Math.min(1, v));
    const at = Math.round(master * (TICKS - 1));
    tick.forEach((t, i) => { t.className = i === at ? "at" : i < at ? "on" : ""; });
    vol.setAttribute("aria-valuenow", Math.round(master * 100));
    if (gain) gain.gain.value = master;
  };
  setVol(master);
  vol.addEventListener("keydown", (ev) => {
    if (ev.key === "ArrowRight" || ev.key === "ArrowUp") { setVol(master + 0.025); ev.preventDefault(); }
    if (ev.key === "ArrowLeft" || ev.key === "ArrowDown") { setVol(master - 0.025); ev.preventDefault(); }
  });
  vol.addEventListener("pointerdown", (ev) => {
    const go = (e) => { const r = vol.getBoundingClientRect(); setVol((e.clientX - r.left) / r.width); };
    go(ev);
    window.addEventListener("pointermove", go);
    window.addEventListener("pointerup", () => window.removeEventListener("pointermove", go), { once: true });
  });

  // ---- MIDI: the computer's ports, in and out, as the FM-1's USB (GLO's MIDI row says what goes where)
  let midiOuts = [];
  function midiOn() {
    if (!navigator.requestMIDIAccess) return;
    navigator.requestMIDIAccess().then((acc) => {
      const wire = () => {
        midiOuts = [...acc.outputs.values()];
        acc.inputs.forEach((inp) => {
          inp.onmidimessage = (ev) => {
            const [s, a = 0, b = 0] = ev.data;
            if (s >= 0x80 && s !== 0xF0) send({ t: "midi", s, a, b });
          };
        });
      };
      wire();
      acc.onstatechange = wire;
    }).catch(() => {});
  }
  function onmsg(m) {
    if (m.t === "store") { storeKeep(m.st); return; }
    if (m.t === "midiout") {
      for (const v of m.mo) {
        const s = v & 255, msg = s >= 0xF8 ? [s] : [s, (v >> 8) & 127, (v >> 16) & 127];
        midiOuts.forEach((o) => { try { o.send(msg); } catch (e) { /* a port gone */ } });
      }
      return;
    }
    if (m.t !== "frame") return;
    const d = img.data, fb = m.fb, pal = m.pal;
    for (let i = 0; i < 240 * 240; i++) {
      const c = pal[i & 1 ? fb[i >> 1] & 15 : fb[i >> 1] >> 4];
      d[i * 4] = c >> 16; d[i * 4 + 1] = (c >> 8) & 255; d[i * 4 + 2] = c & 255; d[i * 4 + 3] = 255;
    }
    lg.putImageData(img, 0, 0);
    window.dispatchEvent(new Event("demake-frame"));    /* the screens' section shows it too */
    for (let k = 0; k < 27; k++) { const on = m.leds[k] >= 64; if (on !== keyEls[k].lit) { keyEls[k].lit = on; light(keyEls[k], on, on || isBlack(k) ? INK : PAPER); } }
    for (let b = 0; b < 14; b++) { const on = m.leds[27 + b] >= 64, r = btnEls[b].r; if (on !== r.lit) { r.lit = on; light(r, on, on ? INK : PAPER); } }
  }

  // ---- the controls
  const held = new Map(), latched = new Set();
  svg.addEventListener("pointerdown", (ev) => {
    const t = ev.target;
    if (t.dataset.k !== undefined) { send({ t: "key", k: +t.dataset.k, d: 1 }); held.set(ev.pointerId, { t: "key", k: +t.dataset.k }); }
    else if (t.dataset.b !== undefined) {
      const b = +t.dataset.b;
      if (ev.shiftKey) { const on = !latched.has(b); on ? latched.add(b) : latched.delete(b); send({ t: "btn", b, d: on ? 1 : 0 }); return; }   /* Shift-click holds it */
      send({ t: "btn", b, d: 1 }); held.set(ev.pointerId, { t: "btn", b });
    }
    else {
      const g = t.closest("g");
      if (g && g.dataset.e !== undefined) held.set(ev.pointerId, { t: "drag", e: +g.dataset.e, y: ev.clientY });
    }
    svg.setPointerCapture(ev.pointerId);
  });
  svg.addEventListener("pointermove", (ev) => {
    const h = held.get(ev.pointerId);
    if (!h || h.t !== "drag") return;
    const steps = Math.trunc((h.y - ev.clientY) / 6);
    if (steps) { turn(h.e, steps); h.y -= steps * 6; }
  });
  const up = (ev) => {
    const h = held.get(ev.pointerId);
    if (!h) return;
    if (h.t === "key") send({ t: "key", k: h.k, d: 0 });
    if (h.t === "btn") send({ t: "btn", b: h.b, d: 0 });
    held.delete(ev.pointerId);
  };
  svg.addEventListener("pointerup", up);
  svg.addEventListener("pointercancel", up);
  /* a wheel: a notch (lines or pages, or a mouse's big pixel step) one step; a trackpad's run of small moves one step
     every 50 px, at most one an event (its glide after flew through the values), from zero again when it turns back
     or rests */
  const wacc = {}, wlast = {};
  const wheelSteps = (e, ev) => {
    if (ev.deltaMode) return ev.deltaY < 0 ? 1 : ev.deltaY > 0 ? -1 : 0;
    const now = performance.now(), d = -ev.deltaY;
    if (now - (wlast[e] || 0) > 300 || Math.sign(d) !== Math.sign(wacc[e] || 0)) wacc[e] = 0;
    wlast[e] = now;
    wacc[e] += d;
    if (Math.abs(wacc[e]) < 50) return 0;
    const n = Math.sign(wacc[e]);
    wacc[e] = Math.max(-49, Math.min(49, wacc[e] - n * 50));
    return n;
  };
  svg.addEventListener("wheel", (ev) => {
    const g = ev.target.closest && ev.target.closest("g");
    if (!g || g.dataset.e === undefined) return;
    ev.preventDefault();
    const n = wheelSteps(+g.dataset.e, ev);
    if (n) turn(+g.dataset.e, n);
  }, { passive: false });

  /* the running machine, for the screens' section: on, start, press a button, turn a knob, its screen */
  window.demakeLive = { on: () => !!node, start: () => power.click(), btn: (b, d) => send({ t: "btn", b, d }), turn: (e, d) => turn(e, d), lcd };

  function turn(e, d) {
    const k = encEls[e];
    k.angle += d * 12;
    k.mark.setAttribute("transform", `rotate(${k.angle} ${k.cx} ${k.cy})`);
    if (e === 7) { setVol(master + d * 0.04); return; }
    send({ t: "enc", e, d });
  }

  // the computer's keyboard, as the Felucca emulator has it: F3..A4 on the Z row (white) and the A row (black), B4..G5
  // on Q..O, the twelve buttons on the number row in the panel's order, [ ] OCT, the arrows SELECT and ALGORITHM
  const whites = [];
  for (let k = 0; k < 27; k++) if (!isBlack(k)) whites.push(k);
  const KEYMAP = {};
  ["KeyZ", "KeyX", "KeyC", "KeyV", "KeyB", "KeyN", "KeyM", "Comma", "Period", "Slash"].forEach((c, i) => KEYMAP[c] = { k: whites[i] });
  [["KeyS", 1], ["KeyD", 3], ["KeyF", 5], ["KeyH", 8], ["KeyJ", 10], ["KeyL", 13], ["Semicolon", 15], ["Quote", 17]].forEach(([c, k]) => KEYMAP[c] = { k });
  ["KeyQ", "KeyW", "KeyE", "KeyR", "KeyT", "KeyY", "KeyU", "KeyI", "KeyO"].forEach((c, i) => KEYMAP[c] = { k: 18 + i });
  ["Digit1", "Digit2", "Digit3", "Digit4", "Digit5", "Digit6", "Digit7", "Digit8", "Digit9", "Digit0", "Minus", "Equal"].forEach((c, i) => KEYMAP[c] = { b: i });
  KEYMAP.BracketLeft = { b: 12 }; KEYMAP.BracketRight = { b: 13 };
  const ARROWS = { ArrowUp: [0, 1], ArrowDown: [0, -1], ArrowRight: [1, 1], ArrowLeft: [1, -1] };
  const down = new Set();
  window.addEventListener("keydown", (ev) => {
    if (ev.metaKey || ev.ctrlKey || ev.altKey || (ev.target.closest && ev.target.closest("#vol, input, textarea"))) return;
    if (ARROWS[ev.code]) { if (!node) return; const [e, d] = ARROWS[ev.code]; turn(e, d); ev.preventDefault(); return; }
    const m = KEYMAP[ev.code];
    if (!m || !node) return;
    ev.preventDefault();
    if (ev.repeat || down.has(ev.code)) return;
    down.add(ev.code);
    send(m.k !== undefined ? { t: "key", k: m.k, d: 1 } : { t: "btn", b: m.b, d: 1 });
  });
  window.addEventListener("keyup", (ev) => {
    const m = KEYMAP[ev.code];
    if (!m || !down.has(ev.code)) return;
    down.delete(ev.code);
    send(m.k !== undefined ? { t: "key", k: m.k, d: 0 } : { t: "btn", b: m.b, d: 0 });
  });
  window.addEventListener("blur", () => { for (const c of down) { const m = KEYMAP[c]; send(m.k !== undefined ? { t: "key", k: m.k, d: 0 } : { t: "btn", b: m.b, d: 0 }); } down.clear(); });
})();

// ---- the 8-bit light, as the screen draws it: every lit thing steps at random through the dither's six arrangements
// (tools/gen_art.py makes the same six), so its bits move and its light stays; the art is drawn here at 2 px a cell
// through the ground, a deep blue and the thermal ramp
(() => {
  "use strict";
  const still = matchMedia("(prefers-reduced-motion: reduce)").matches;
  const B = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5], at = (x, y) => B[(y & 3) * 4 + (x & 3)];
  const B6 = [(x, y) => at(x, y), (x, y) => at(x + 2, y + 1), (x, y) => at(3 - x, y), (x, y) => at(x, 3 - y),
    (x, y) => at(y, x), (x, y) => at(x + 1, y + 3)].map((f) => { const m = new Uint8Array(16); for (let y = 0; y < 4; y++) for (let x = 0; x < 4; x++) m[y * 4 + x] = f(x, y); return m; });
  const rnd = (n, not) => { let k = (Math.random() * n) | 0; if (k === not) k = (k + 1) % n; return k; };

  // the rings (border-image) and the sprites (six frames, one under another)
  for (const r of [24, 12]) for (let k = 0; k < 6; k++) new Image().src = `img/bits/ring${r}_${k}.png`;
  for (let k = 0; k < 6; k++) { new Image().src = `img/bits/spark_${k}.png`; new Image().src = `img/bits/glint_${k}.png`; }
  const glinted = [...document.querySelectorAll(".nav, .foot a, .mside a, .mpager a")], here = document.querySelector(".nav[aria-current]");
  const sprites = [...document.querySelectorAll(".bits")], ringed = [...document.querySelectorAll(".lit, .pill, .tiles .on, .page")];
  const visible = new Set();
  const io = new IntersectionObserver((es) => es.forEach((e) => e.isIntersecting ? visible.add(e.target) : visible.delete(e.target)));
  [...sprites, ...ringed].forEach((el) => io.observe(el));

  // the art: each piece's heat (a grey PNG) laid through the tones at 2 px a cell, dithered
  const TONES = ["141416", "10229a", "2459ff", "5a48f6", "8e3aea", "c02fd2", "e52bc2", "ff45c2", "ff8cda", "ffddf2"].map((h) => [0, 2, 4].map((i) => parseInt(h.slice(i, i + 2), 16)));
  const POS = [0.2, 0.34, 0.46, 0.53, 0.6, 0.66, 0.72, 0.81, 0.9, 1];
  const LI = new Uint8Array(256), LF = new Uint8Array(256);
  for (let v = 0; v < 256; v++) {
    const t = v / 255;
    let i = 0;
    while (i < POS.length - 1 && t >= POS[i + 1]) i++;
    let f = t <= POS[0] ? 0 : i >= POS.length - 1 ? 0 : Math.round((t - POS[i]) / (POS[i + 1] - POS[i]) * 16);
    if (f >= 16) { i++; f = 0; }
    LI[v] = Math.min(i, 9); LF[v] = f;
  }
  const heats = [...document.querySelectorAll("canvas.heat")].map((cv) => {
    const h = { cv, ctx: cv.getContext("2d"), img: new Image(), w: 0, h: 0, idx: null, frac: null, data: null, pat: 0 };
    h.img.onload = () => size(h);
    h.img.src = cv.dataset.heat;
    new ResizeObserver(() => size(h)).observe(cv.parentElement);
    io.observe(cv);
    return h;
  });
  function size(h) {
    if (!h.img.complete || !h.img.naturalWidth) return;
    const r = h.cv.parentElement.getBoundingClientRect(), w = Math.ceil(r.width / 2), ht = Math.ceil(r.height / 2);
    if (!w || !ht || (w === h.w && ht === h.h)) return;
    h.w = w; h.h = ht;
    h.cv.width = w; h.cv.height = ht; h.cv.style.width = w * 2 + "px"; h.cv.style.height = ht * 2 + "px";
    const o = document.createElement("canvas"); o.width = w; o.height = ht;
    const oc = o.getContext("2d"), iw = h.img.naturalWidth, ih = h.img.naturalHeight, k = Math.max(w / iw, ht / ih);
    oc.imageSmoothingEnabled = true;
    oc.drawImage(h.img, (w - iw * k) / 2, (ht - ih * k) / 2, iw * k, ih * k);   /* cover */
    const src = oc.getImageData(0, 0, w, ht).data;
    h.idx = new Uint8Array(w * ht); h.frac = new Uint8Array(w * ht);
    for (let j = 0; j < w * ht; j++) { const v = src[j * 4]; h.idx[j] = LI[v]; h.frac[j] = LF[v]; }
    h.data = h.ctx.createImageData(w, ht);
    draw(h);
  }
  function draw(h) {
    if (!h.data) return;
    const d = h.data.data, m = B6[h.pat];
    for (let y = 0, j = 0; y < h.h; y++)
      for (let x = 0; x < h.w; x++, j++) {
        let i = h.idx[j];
        if (h.frac[j] > m[(y & 3) * 4 + (x & 3)] && i < 9) i++;
        const c = TONES[i];
        d[j * 4] = c[0]; d[j * 4 + 1] = c[1]; d[j * 4 + 2] = c[2]; d[j * 4 + 3] = 255;
      }
    h.ctx.putImageData(h.data, 0, 0);
  }

  // what the pointer is on heats: its bits run every 60 ms
  const hot = new Set();
  [...ringed, ...glinted].forEach((el) => { el.addEventListener("pointerenter", () => hot.add(el)); el.addEventListener("pointerleave", () => hot.delete(el)); });
  if (!still) setInterval(() => {
    for (const el of hot) {
      const k = rnd(6), j = rnd(6);
      el.style.setProperty("--ring24", `url(img/bits/ring24_${k}.png)`); el.style.setProperty("--ring12", `url(img/bits/ring12_${k}.png)`);
      el.style.setProperty("--spark", `url(img/bits/spark_${j}.png)`); el.style.setProperty("--spark2", `url(img/bits/spark_${(j + 3) % 6}.png)`);
      el.style.setProperty("--glint", `url(img/bits/glint_${j}.png)`);
    }
  }, 60);

  // the clock: every 140 ms, about half of what shows takes a new arrangement
  if (!still) setInterval(() => {
    if (document.hidden) return;
    for (const el of sprites) if (visible.has(el) && Math.random() < 0.5) { const k = rnd(6, +el.dataset.k); el.dataset.k = k; el.style.backgroundPositionY = k * 20 + "%"; }
    for (const el of ringed) if (visible.has(el) && Math.random() < 0.5) { const k = rnd(6); el.style.setProperty("--ring24", `url(img/bits/ring24_${k}.png)`); el.style.setProperty("--ring12", `url(img/bits/ring12_${k}.png)`); }
    for (const h of heats) if (visible.has(h.cv) && Math.random() < 0.5) { h.pat = rnd(6, h.pat); draw(h); }
    if (here && !hot.has(here) && Math.random() < 0.5) here.style.setProperty("--glint", `url(img/bits/glint_${rnd(6)}.png)`);
  }, 140);

  // the screens: a page pressed is shown, lit in the index; the five marks belong to Home's anatomy. Once the machine
  // runs (Power on, or a page pressed here starts it), the screen is the machine's own, live: a page pressed presses
  // its button, and a value scrolled (or dragged) under the pointer turns its knob, as on the panel above
  const pages = [...document.querySelectorAll(".page")], shown = document.getElementById("shown"), name = document.getElementById("shownname");
  const main = document.querySelector(".main"), live = document.getElementById("live"), ih = document.querySelector(".ih span");
  if (!shown || !live) return;
  const lctx = live.getContext("2d");
  let liveOn = false, pictured = false;
  const frame = () => new Promise((res) => { const f = () => { window.removeEventListener("demake-frame", f); res(); }; window.addEventListener("demake-frame", f); setTimeout(f, 4000); });
  window.addEventListener("demake-frame", () => {
    const E = window.demakeLive;
    if (!liveOn) { liveOn = true; ih.textContent = "Live: scroll a value to turn it"; }
    live.hidden = pictured; shown.hidden = !pictured;
    if (!pictured) { lctx.imageSmoothingEnabled = false; lctx.drawImage(E.lcd, 0, 0); }
  });
  pages.forEach((p) => { new Image().src = p.dataset.src; p.addEventListener("click", async () => {
    const E = window.demakeLive;
    pages.forEach((q) => q.classList.toggle("on", q === p));
    shown.src = p.dataset.src; shown.alt = p.dataset.name;
    name.innerHTML = `<b>${p.dataset.name}</b> ${p.dataset.say}`;
    main.classList.toggle("home", p === pages[0]);
    pictured = p.dataset.b === undefined;               /* the night theme: its picture */
    if (!E || pictured) { if (liveOn) { live.hidden = true; shown.hidden = false; } return; }
    if (!E.on()) { E.start(); await frame(); }
    const b = +p.dataset.b;
    E.btn(b, 1);
    setTimeout(() => E.btn(b, 0), +(p.dataset.hold || 60));
  }); });
  // a knob under the pointer: the head SELECT, the four fields K1-K4, the foot ALGORITHM
  const knobAt = (ev) => {
    const r = live.getBoundingClientRect(), x = (ev.clientX - r.left) * 240 / r.width, y = (ev.clientY - r.top) * 240 / r.height;
    return y < 40 ? 0 : y < 102 ? 3 + Math.min(3, Math.max(0, Math.floor(x / 60))) : y > 218 ? 1 : -1;
  };
  live.addEventListener("wheel", (ev) => {
    const E = window.demakeLive, e = knobAt(ev);
    if (!E || !E.on() || e < 0) return;
    ev.preventDefault();
    const n = wheelSteps(e, ev);
    if (n) E.turn(e, n);
  }, { passive: false });
  live.addEventListener("pointerdown", (ev) => {
    const E = window.demakeLive, e = knobAt(ev);
    if (!E || !E.on() || e < 0) return;
    let y0 = ev.clientY;
    live.setPointerCapture(ev.pointerId);
    const mv = (m) => { const st = Math.trunc((y0 - m.clientY) / 6); if (st) { E.turn(e, st); y0 -= st * 6; } };
    live.addEventListener("pointermove", mv);
    live.addEventListener("pointerup", () => live.removeEventListener("pointermove", mv), { once: true });
  });
})();
