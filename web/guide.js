// SPDX-License-Identifier: GPL-3.0-only
// "How to play" (the owner: as MONO2K's site has it): the FM-1's panel in outline beside a list of its controls.
// Pointing at one, on the drawing or in the list, lights it in the screen's 8-bit light, says what it does and its
// key in the emulator, and shows its page on the drawing's screen (the tour's pictures). Idle, it walks the controls.
(() => {
  "use strict";
  const svg = document.getElementById("hsvg"), list = document.getElementById("hlist");
  if (!svg || !list) return;
  const NS = "http://www.w3.org/2000/svg";
  const say = document.getElementById("hsay"), key = document.getElementById("hkey");
  /* the panel's geometry, as app.js draws the emulator's */
  const WHITE_X = [71, 119, 167, 215, 264, 312, 360, 408, 456, 504, 553, 601, 649, 697, 745, 794];
  const BLACK_X = [95, 143, 191, 288, 336, 432, 480, 529, 625, 673, 769];
  const isBlack = (k) => (0x54a >> ((k + 5) % 12)) & 1;
  const KEYR = [];
  for (let k = 0, w = 0, b = 0; k < 27; k++) KEYR.push(isBlack(k) ? [BLACK_X[b++], 330, 38, 70] : [WHITE_X[w++], 412, 44, 112]);
  const BTNR = [];
  for (let i = 0; i < 12; i++) BTNR.push([[500, 554, 608, 661, 715, 768][i % 6], [177, 238][(i / 6) | 0], 40, 34]);
  BTNR.push([87, 256, 48, 26], [153, 256, 48, 26]);
  const BTN = ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT−", "OCT+"];
  const ENC = ["Select", "Algorithm", "Presets", "K1", "K2", "K3", "K4", "Master"];
  const ENC_CX = [186.5, 186.5, 95, 508, 604, 701, 797, 95], ENC_CY = [101, 191, 191, 101, 101, 101, 101, 101];
  const LCD = [247, 59, 201];
  const S = (n) => `img/screens/${n}.png`;
  /* every control: its name, what it lights (b: buttons, e: knobs, k: keys), what it does, its key, its page */
  const WH = [], BL = [];
  for (let k = 0; k < 27; k++) (isBlack(k) ? BL : WH).push(k);
  const G = [
    { t: "HOME", r: "Home", b: [6], say: "Levels and tempo, sends to the Space, pans. Held: the scenes", key: "7", img: "01_home" },
    { t: "SEQ", r: "Roll", b: [9], say: "This synth's roll: write, record, play its pattern", key: "0", img: "02_roll" },
    { t: "ARP", r: "Chord", b: [8], say: "The chords, the key; its Keys row: chords from the keys", key: "9", img: "07_chord" },
    { t: "EDIT", r: "Sound", b: [4], say: "The recording, its layer, the voice, the loop", key: "5", img: "03_sound" },
    { t: "SEL", r: "Filter", b: [1], say: "Cutoff, resonance, envelope, drive", key: "2", img: "04_filter" },
    { t: "ENV", r: "Amp", b: [2], say: "The envelope and the level", key: "3", img: "05_amp" },
    { t: "FX", r: "Effects", b: [0], say: "This synth's reverb, delay, motion and sends", key: "1", img: "06_fx" },
    { t: "LFO", r: "Wave", b: [3], say: "A slow wave on the pitch, cutoff, level, pan or start", key: "4", img: "09_lfo" },
    { t: "GLO", r: "Global", b: [5], say: "Over the mix: colour, the Space, old media; the setup", key: "6", img: "08_global" },
    { t: "SAVE", r: "Save", b: [7], say: "Projects and sounds; again, saved", key: "8", img: "30_save_projects" },
    { t: "PLAY", r: "", b: [10], say: "The rolls and the chords play; again, stop", key: "−", img: "02_roll" },
    { t: "REC", r: "", b: [11], say: "Lit, the keys are recorded as played", key: "=", img: "02_roll" },
    { t: "Keys", r: "Notes", k: [...WH, ...BL], say: "The synth selected; on Chord, or Keys at All, chords", key: "Z … /, Q … O", img: "07_chord" },
    { t: "OCT", r: "Octave", b: [12, 13], say: "An octave down or up. Both held: the landscape", key: "[ ]", img: "01_home" },
    { t: "SELECT", r: "Synth", e: [0], say: "The synth, 1 to 3, on every page", key: "↑ ↓", img: "01_home" },
    { t: "PRESETS", r: "Rows", e: [2], say: "The page's rows; on Home, the synth's sound", key: "drag it", img: "03_sound" },
    { t: "ALGO", r: "Type", e: [1], say: "The value at the screen's foot: a row's type", key: "← →", img: "06_fx" },
    { t: "K1 … K4", r: "Values", e: [3, 4, 5, 6], say: "The four values at the top of the screen", key: "drag them", img: "05_amp" },
    { t: "MASTER", r: "Volume", e: [7], say: "The volume", key: "the Volume ticks", img: "01_home" },
  ];
  const el = (t, a, p) => { const e = document.createElementNS(NS, t); for (const k in a) e.setAttribute(k, a[k]); (p || svg).appendChild(e); return e; };
  const label = (x, y, s, size, cls) => { const t = el("text", { x, y, "text-anchor": "middle", "font-size": size, class: cls || "hl" }); t.textContent = s; return t; };
  /* the light: a sprite of the lit shape over a key or a button (its bits stepping, as the emulator's), the spark's
   * texture in a knob */
  const defs = el("defs", {}), pat = el("pattern", { id: "hspark", width: 24, height: 24, patternUnits: "userSpaceOnUse" }, defs);
  const sparkIm = el("image", { width: 24, height: 24, href: "img/bits/spark_0.png", preserveAspectRatio: "none" }, pat);
  sparkIm.style.imageRendering = "pixelated";
  const lits = [];
  const litImg = (x, y, w, h, shape, g) => {
    const im = el("image", { x, y, width: w, height: h, href: `img/bits/${shape}_0.png`, preserveAspectRatio: "none", class: "hlit" }, g);
    im.style.imageRendering = "pixelated"; im.dataset.shape = shape;
    lits.push(im);
    return im;
  };
  el("rect", { x: 20, y: 20, width: 864, height: 526, rx: 44, class: "hbody" });
  el("rect", { x: LCD[0] - 9, y: LCD[1] - 9, width: LCD[2] + 18, height: LCD[2] + 18, rx: 16, class: "hscr" });
  const shot = el("image", { x: LCD[0], y: LCD[1], width: LCD[2], height: LCD[2], href: S("01_home"), class: "hshot" });
  shot.style.imageRendering = "pixelated";
  el("line", { x1: 46, y1: 316, x2: 858, y2: 316, class: "hline" });
  const ctl = {};                                       /* "b3", "e1", "k12": its shape and its light */
  KEYR.forEach(([x, y, w, h], k) => {
    const b = isBlack(k), g = el("g", { "data-c": "k" + k, class: "hc" });
    el("rect", { x: x - w / 2, y, width: w, height: h, rx: 10, class: b ? "hk hb" : "hk" }, g);
    ctl["k" + k] = { g, lit: litImg(x - w / 2, y, w, h, b ? "keyb" : "keyw", g) };
  });
  BTNR.forEach(([x, y, w, h], i) => {
    const g = el("g", { "data-c": "b" + i, class: "hc" });
    el("rect", { x: x - w / 2, y: y - h / 2, width: w, height: h, rx: 12, class: "hk" }, g);
    const lit = litImg(x - w / 2, y - h / 2, w, h, i < 12 ? "btn" : "oct", g);
    label(x, y + 5, BTN[i], i < 12 ? 12 : 13).setAttribute("pointer-events", "none");
    ctl["b" + i] = { g, lit };
  });
  ENC_CX.forEach((cx, e) => {
    const big = e >= 3 && e <= 6, r = big ? 24 : 20, cy = ENC_CY[e], g = el("g", { "data-c": "e" + e, class: "hc" });
    el("circle", { cx, cy, r, class: "hk" }, g);
    const lit = el("circle", { cx, cy, r: r - 1, fill: "url(#hspark)", class: "hlit" }, g);
    el("line", { x1: cx, y1: cy - r + 4, x2: cx, y2: cy - r + 12, class: "hmark" }, g);
    label(cx, cy - r - 9, ENC[e], 15);
    ctl["e" + e] = { g, lit };
  });
  /* the list: a button a control */
  list.innerHTML = G.map((c, i) => `<button type="button" class="page hb${i}" data-i="${i}"><b>${c.t}</b><span>${c.r}</span></button>`).join("");
  const btns = [...list.children];
  for (const c of G) new Image().src = S(c.img);
  const of = (c) => [...(c.b || []).map((b) => "b" + b), ...(c.e || []).map((e) => "e" + e), ...(c.k || []).map((k) => "k" + k)];
  const owner = {};
  G.forEach((c, i) => of(c).forEach((id) => { owner[id] = i; }));
  let cur = -1;
  function pick(i) {
    if (i === cur || !G[i]) return;
    cur = i;
    const c = G[i], on = new Set(of(c));
    for (const id in ctl) ctl[id].g.classList.toggle("on", on.has(id));
    btns.forEach((b, j) => b.classList.toggle("on", j === i));
    say.innerHTML = `<b>${c.t}${c.r && c.r !== c.t ? " · " + c.r : ""}</b> <span>${c.say}</span>`;
    key.textContent = "In the emulator: " + c.key;
    shot.setAttribute("href", S(c.img));
  }
  /* pointing: the drawing or the list; a tap does the same */
  let touched = false;
  const touch = (i) => { touched = true; pick(i); };
  svg.addEventListener("pointerover", (ev) => { const g = ev.target.closest && ev.target.closest("[data-c]"); if (g && owner[g.dataset.c] !== undefined) touch(owner[g.dataset.c]); });
  svg.addEventListener("click", (ev) => { const g = ev.target.closest && ev.target.closest("[data-c]"); if (g && owner[g.dataset.c] !== undefined) touch(owner[g.dataset.c]); });
  list.addEventListener("pointerover", (ev) => { const b = ev.target.closest && ev.target.closest("button"); if (b) touch(+b.dataset.i); });
  list.addEventListener("focusin", (ev) => { const b = ev.target.closest && ev.target.closest("button"); if (b) touch(+b.dataset.i); });
  list.addEventListener("click", (ev) => { const b = ev.target.closest && ev.target.closest("button"); if (b) touch(+b.dataset.i); });
  /* the light's bits stepping (140 ms, the screen's frame); idle and in view, a walk through the controls */
  const calm = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  let inView = false, walk = 0;
  if ("IntersectionObserver" in window) new IntersectionObserver((es) => { for (const e of es) inView = e.isIntersecting; }).observe(svg);
  setInterval(() => {
    if (!inView || document.hidden) return;
    if (!calm) {
      for (const im of lits) if (Math.random() < 0.5) im.setAttribute("href", `img/bits/${im.dataset.shape}_${(Math.random() * 6) | 0}.png`);
      sparkIm.setAttribute("href", `img/bits/spark_${(Math.random() * 6) | 0}.png`);
    }
    if (!touched && !calm && ++walk % 18 === 0) pick((cur + 1) % G.length);   /* (every 2.5 s) */
  }, 140);
  pick(0);
})();
