// SPDX-License-Identifier: GPL-3.0-only
// Demake's emulator page: the FM-1's panel (its geometry as the community's emulators measured it, as MONO2K's
// panel.js has it), the screen from the worklet, and the controls from mouse, touch and the computer's keyboard.
(() => {
  "use strict";
  const NS = "http://www.w3.org/2000/svg";
  const WHITE_X = [71, 119, 167, 215, 264, 312, 360, 408, 456, 504, 553, 601, 649, 697, 745, 794];
  const BLACK_X = [95, 143, 191, 288, 336, 432, 480, 529, 625, 673, 769];
  const isBlack = (k) => (0x54a >> ((k + 5) % 12)) & 1;
  const KEYR = [];
  for (let k = 0, w = 0, b = 0; k < 27; k++) KEYR.push(isBlack(k) ? [BLACK_X[b++], 326, 42, 76] : [WHITE_X[w++], 408, 42, 76]);
  const BTNR = [];
  for (let i = 0; i < 12; i++) BTNR.push([[500, 554, 608, 661, 715, 768][i % 6], [177, 231][(i / 6) | 0], 36, 36]);
  BTNR.push([87, 250, 42, 22], [153, 250, 42, 22]);
  const BTN = ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT−", "OCT+"];
  const ENC = ["SELECT", "ALGORITHM", "PRESETS", "K1", "K2", "K3", "K4", "MASTER"];
  const ENC_CX = [186.5, 186.5, 95, 508, 604, 701, 797, 95], ENC_CY = [101, 191, 191, 101, 101, 101, 101, 101];
  const LCD = [247, 59, 201];
  const VB = [14, 14, 876, 538];

  const svg = document.getElementById("svg"), lcd = document.getElementById("lcd"), panel = document.getElementById("panel");
  const lg = lcd.getContext("2d"), img = lg.createImageData(240, 240);
  const el = (t, a, p) => { const e = document.createElementNS(NS, t); for (const k in a) e.setAttribute(k, a[k]); (p || svg).appendChild(e); return e; };

  // ---- the panel
  el("rect", { x: 18, y: 18, width: 868, height: 530, rx: 26, fill: "#2a2b30" });
  el("rect", { x: LCD[0] - 10, y: LCD[1] - 10, width: LCD[2] + 20, height: LCD[2] + 20, rx: 14, fill: "#101114" });
  const keyEls = KEYR.map(([x, y, w, h], k) => {
    const r = el("rect", { x: x - w / 2, y, width: w, height: h, rx: 7, fill: isBlack(k) ? "#3a3c44" : "#d8d6d0", stroke: "#101114", "stroke-width": 2 });
    r.dataset.k = k;
    return r;
  });
  const btnEls = BTNR.map(([x, y, w, h], i) => {
    const r = el("rect", { x: x - w / 2, y: y - h / 2, width: w, height: h, rx: Math.min(w, h) / 2, fill: "#c9c6c0", stroke: "#101114", "stroke-width": 2 });
    r.dataset.b = i;
    const t = el("text", { x, y: y + h / 2 + 15, "text-anchor": "middle", fill: "#b3b1ac", "font-size": 11, "font-family": "Tomato, sans-serif", "letter-spacing": 1 });
    t.textContent = BTN[i];
    return r;
  });
  const encEls = ENC_CX.map((cx, e) => {
    const big = e >= 3 && e <= 6, r = big ? 26 : 22;
    const g = el("g", { "data-e": e });
    el("circle", { cx, cy: ENC_CY[e], r, fill: e === 7 ? "#4a4c55" : "#3a3c44", stroke: "#101114", "stroke-width": 2 }, g);
    const mark = el("line", { x1: cx, y1: ENC_CY[e] - r + 5, x2: cx, y2: ENC_CY[e] - r + 14, stroke: "#f0a33c", "stroke-width": 3, "stroke-linecap": "round" }, g);
    const t = el("text", { x: cx, y: ENC_CY[e] + r + 15, "text-anchor": "middle", fill: "#b3b1ac", "font-size": 11, "font-family": "Tomato, sans-serif", "letter-spacing": 1 });
    t.textContent = ENC[e];
    g.dataset.e = e;
    return { g, mark, cx, cy: ENC_CY[e], angle: 0 };
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
  lg.fillStyle = "#1f2c66"; lg.fillRect(0, 0, 240, 240);
  lg.fillStyle = "#dedcd7"; lg.font = "500 14px Tomato, sans-serif"; lg.textAlign = "center"; lg.fillText("POWER ON", 120, 124);

  // ---- the sound: the core in a worklet
  let ctx = null, node = null, gain = null, master = 0.8;
  const send = (m) => { if (node) node.port.postMessage(m); };
  const status = document.getElementById("status");
  document.getElementById("power").addEventListener("click", async () => {
    if (ctx) { ctx.resume(); return; }
    try {
      ctx = new AudioContext({ sampleRate: 44100 });
      await ctx.audioWorklet.addModule("worklet.js?v=1");
      node = new AudioWorkletNode(ctx, "demake", { outputChannelCount: [2] });
      gain = ctx.createGain(); gain.gain.value = master;
      node.connect(gain).connect(ctx.destination);
      const bytes = await (await fetch("dmk.wasm?v=1")).arrayBuffer();
      node.port.onmessage = (ev) => onmsg(ev.data);
      node.port.postMessage({ t: "wasm", bytes, seed: (Math.random() * 4294967296) >>> 0 });
      status.textContent = "On";
    } catch (e) { status.textContent = "No sound: " + e.message; }
  });

  function onmsg(m) {
    if (m.t !== "frame") return;
    const d = img.data, fb = m.fb, pal = m.pal;
    for (let i = 0; i < 240 * 240; i++) {
      const c = pal[i & 1 ? fb[i >> 1] & 15 : fb[i >> 1] >> 4];
      d[i * 4] = c >> 16; d[i * 4 + 1] = (c >> 8) & 255; d[i * 4 + 2] = c & 255; d[i * 4 + 3] = 255;
    }
    lg.putImageData(img, 0, 0);
    for (let k = 0; k < 27; k++) keyEls[k].setAttribute("fill", m.leds[k] >= 64 ? "#f0a33c" : isBlack(k) ? "#3a3c44" : "#d8d6d0");
    for (let b = 0; b < 14; b++) btnEls[b].setAttribute("fill", m.leds[27 + b] >= 64 ? "#f0a33c" : "#c9c6c0");
  }

  // ---- the controls
  const held = new Map();
  svg.addEventListener("pointerdown", (ev) => {
    const t = ev.target;
    if (t.dataset.k !== undefined) { send({ t: "key", k: +t.dataset.k, d: 1 }); held.set(ev.pointerId, { t: "key", k: +t.dataset.k }); }
    else if (t.dataset.b !== undefined) { send({ t: "btn", b: +t.dataset.b, d: 1 }); held.set(ev.pointerId, { t: "btn", b: +t.dataset.b }); }
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
  svg.addEventListener("wheel", (ev) => {
    const g = ev.target.closest && ev.target.closest("g");
    if (!g || g.dataset.e === undefined) return;
    ev.preventDefault();
    turn(+g.dataset.e, ev.deltaY < 0 ? 1 : -1);
  }, { passive: false });

  function turn(e, d) {
    const k = encEls[e];
    k.angle += d * 12;
    k.mark.setAttribute("transform", `rotate(${k.angle} ${k.cx} ${k.cy})`);
    if (e === 7) { master = Math.max(0, Math.min(1, master + d * 0.04)); if (gain) gain.gain.value = master; return; }
    send({ t: "enc", e, d });
  }

  // the computer's keyboard: two rows of keys from F3, Z and X the octave
  const WHITE = "asdfghjkl;'", BLACK = "wer" + "yu" + "op[";
  const whites = [], blacks = [];
  for (let k = 0; k < 27; k++) (isBlack(k) ? blacks : whites).push(k);
  const keyOf = (c) => { const w = WHITE.indexOf(c); if (w >= 0) return whites[w]; const b = BLACK.indexOf(c); return b >= 0 ? blacks[b] : -1; };
  const down = new Set();
  window.addEventListener("keydown", (ev) => {
    if (ev.repeat || ev.metaKey || ev.ctrlKey) return;
    const c = ev.key.toLowerCase();
    if (c === "z" || c === "x") { send({ t: "btn", b: c === "z" ? 12 : 13, d: 1 }); send({ t: "btn", b: c === "z" ? 12 : 13, d: 0 }); return; }
    const k = keyOf(c);
    if (k >= 0 && !down.has(k)) { down.add(k); send({ t: "key", k, d: 1 }); ev.preventDefault(); }
  });
  window.addEventListener("keyup", (ev) => {
    const k = keyOf(ev.key.toLowerCase());
    if (k >= 0 && down.has(k)) { down.delete(k); send({ t: "key", k, d: 0 }); }
  });
})();
