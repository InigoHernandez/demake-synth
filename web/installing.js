// SPDX-License-Identifier: GPL-3.0-only
// The installing screen (install.html): while the FM-1 is written, the dark ground comes over the page in blocks, by
// the ordered dither, and the wordmark lights as the screen draws its light, 8-bit. Each of its pixels arrives by the
// dither, left to right, exactly as far as the blocks the FM-1 has asked for, and heats from the deep blue to its own
// tone on the thermal ramp (the splash's light, at the pace of the writing). Before that its outline waits: a pass of
// light while the FM-1 checks the package, out and back in blocks while it restarts. Written, a glint of the pale core
// crosses it while the FM-1 starts again; done, its bits step through the six arrangements. Going back to V15 runs it
// the other way: Demake's light goes out as the blocks go in.
// For less motion: no ground in blocks, no pass, no glint, no stepping, no heat; the light still follows the blocks.

const PAL = ["2459ff", "5a48f6", "8e3aea", "c02fd2", "e52bc2", "ff45c2", "ff8cda", "ffddf2"]
  .map((h) => [0, 2, 4].map((i) => parseInt(h.slice(i, i + 2), 16)));
const DEEP = [16, 34, 154], GROUND = [20, 20, 22], GRID = [27, 27, 30], LINE = [42, 42, 45], LABEL = [119, 119, 122];
const B = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5], bay = (x, y) => B[(y & 3) * 4 + (x & 3)];
const HEAT = 900, BLOCK = 16, CURTAIN = 420, TICKS = 41, STAGES = { start: 0, verify: 0, loader: 1, write: 2, reboot: 3, done: 4 };
const calm = () => matchMedia("(prefers-reduced-motion: reduce)").matches;

// a sprite of six arrangements as tones 0..7 (-1 where nothing is lit), each pixel's place in the arrival (the
// splash's: the dither and the column) and whether it is on the shape's edge (the outline that waits)
function sprite(src) {
  return new Promise((ok, no) => {
    const img = new Image();
    img.onload = () => {
      const W = img.naturalWidth, H = img.naturalHeight / 6, c = document.createElement("canvas");
      c.width = W; c.height = H * 6;
      const x = c.getContext("2d"); x.drawImage(img, 0, 0);
      const px = x.getImageData(0, 0, W, H * 6).data, tone = [];
      for (let f = 0; f < 6; f++) {
        const t = new Int8Array(W * H).fill(-1);
        for (let j = 0; j < W * H; j++) {
          const k = (f * W * H + j) * 4;
          if (px[k + 3] < 128) continue;
          let best = 0, bd = 1e9;
          for (let i = 0; i < 8; i++) { const d = Math.abs(px[k] - PAL[i][0]) + Math.abs(px[k + 1] - PAL[i][1]) + Math.abs(px[k + 2] - PAL[i][2]); if (d < bd) { bd = d; best = i; } }
          t[j] = best;
        }
        tone.push(t);
      }
      const at = new Float32Array(W * H), edge = new Uint8Array(W * H), T = tone[0], rows = H > W / 2 ? 2 : 1;
      const on = (x, y) => x >= 0 && y >= 0 && x < W && y < H && T[y * W + x] >= 0;
      for (let y = 0, j = 0; y < H; y++)
        for (let x = 0; x < W; x++, j++) {
          // in reading order (the stacked word's top line, then its foot), a narrow dithered edge: the share reads
          const r = rows > 1 && y >= H / 2 ? 1 : 0;
          at[j] = (bay(x, y) / 16 * 0.1 + (r + x / W) / rows * 0.89) + 0.005;   // 0..0.995: at 100 % all lit
          edge[j] = on(x, y) && !(on(x - 1, y) && on(x + 1, y) && on(x, y - 1) && on(x, y + 1)) ? 1 : 0;
        }
      const lit = new Float32Array(W * H).fill(-1);                  // when each pixel arrived (ms), -1 not yet
      const cv = document.createElement("canvas"); cv.width = W; cv.height = H;
      const g = cv.getContext("2d");
      ok({ W, H, tone, at, edge, lit, cv, g, data: g.createImageData(W, H) });
    };
    img.onerror = () => no(new Error(src));
    img.src = src;
  });
}

export function installing() {
  const $ = (id) => document.getElementById(id);
  const root = $("inst"), box = $("inst-c"), mark = $("inst-mark"), ground = $("inst-ground"), say = $("inst-say");
  const pct = $("inst-pc"), close = $("inst-close"), stages = [...root.querySelectorAll(".inst-st li")];
  const meter = $("inst-ticks"), tick = [];
  for (let i = 0; i < TICKS; i++) tick.push(meter.appendChild(document.createElement("i")));
  const mg = mark.getContext("2d");
  const sprites = {};
  const load = (n) => sprites[n] || (sprites[n] = sprite(n === "wide" ? "img/wordmark.png" : "img/wordmark_stack.png"));
  let s = null, raf = 0, last = 0, frame = 0, lastStep = 0;

  const narrow = () => innerWidth < 800;
  const reset = () => ({
    open: true, out: false, phase: "start", prog: 0, shown: 0, t0: performance.now(), phaseAt: performance.now(),
    curtain: 0, leaving: -1, fail: false, writeAt: 0, writeN: 0, total: 1, verifyN: 0, onClose: null,
  });

  // the content on the module's lines: its top a whole number of rows down, about the middle
  function place() {
    const rows = getComputedStyle(box).gridTemplateRows.split(" ").length * 48;
    box.style.paddingTop = Math.max(48, Math.floor((innerHeight - rows) / 96) * 48) + "px";
  }
  const tell = (t) => { if (say.textContent !== t) say.textContent = t; };   // the live line only when it changes

  function stage(k) {
    const at = STAGES[k] ?? 4;
    stages.forEach((li, i) => { li.className = i < at ? "on" : i === at ? "at" : ""; });
  }

  function meterTo(p) {
    const at = Math.round(p * (TICKS - 1));
    tick.forEach((t, i) => { t.className = i < at ? "on" : i === at && p < 1 ? "at" : i === at ? "on" : ""; });
    pct.textContent = `${Math.floor(p * 100)} %`;
  }

  function open(o) {
    s = reset();
    s.out = !!o.out; s.total = o.total || 1; s.onClose = o.onClose || null;
    $("inst-verb").textContent = o.verb; $("inst-what").textContent = o.what;
    say.textContent = "Keep the cable in.";
    say.className = "inst-say";
    close.hidden = true;
    root.hidden = false; root.classList.toggle("out", s.out);
    root.classList.remove("in", "leave");
    document.documentElement.classList.add("inst-open");
    stage("start"); meterTo(0);
    place();
    load(narrow() ? "stack" : "wide").then(() => {}, () => {});
    load(narrow() ? "wide" : "stack").then(() => {}, () => {});
    for (const n of ["wide", "stack"]) if (sprites[n]) sprites[n].then((sp) => sp.lit.fill(-1), () => {});
    if (calm()) { s.curtain = 1; root.classList.add("in"); }
    root.focus({ preventScroll: true });
    cancelAnimationFrame(raf); raf = requestAnimationFrame(draw);
  }

  function eta(now) {
    if (s.writeN < 40) return "";
    const rate = s.writeN / (now - s.writeAt), left = (s.total - s.writeN) / rate / 1000;
    if (left < 8) return " · nearly there";
    return ` · about ${Math.ceil(left / 5) * 5} s left`;
  }

  function step(k, a) {
    if (!s || !s.open) return;
    const now = performance.now();
    if (k !== s.phase) { s.phase = k; s.phaseAt = now; }
    stage(k);
    if (k === "start") tell(`Found ${a}. Keep the cable in.`);
    else if (k === "verify") { s.verifyN = a; tell("The FM-1 checks the package…"); }
    else if (k === "loader") tell("It restarts into its update mode…");
    else if (k === "write") {
      if (!s.writeAt) { s.writeAt = now; s.writeN = 0; }
      s.writeN = a; s.prog = Math.min(0.99, a / s.total);
      tell((s.out ? "Going back" : "Writing") + eta(now));
    } else if (k === "reboot") { s.prog = 1; tell("Written. It starts again…"); }
  }

  function finish(text, ok) {
    if (!s || !s.open) return;
    s.phase = ok ? "done" : "fail"; s.phaseAt = performance.now(); s.fail = !ok;
    if (ok) { s.prog = 1; stage("done"); }
    say.textContent = text;
    say.className = "inst-say " + (ok ? "ok" : "err");
    close.hidden = false;
    close.textContent = ok ? "Close" : "Back to the page";
    close.focus({ preventScroll: true });
  }

  function leave() {
    if (!s || !s.open || close.hidden) return;
    s.open = false; s.leaving = performance.now();
    root.classList.remove("in"); root.classList.add("leave");
    if (calm()) end();
  }

  function end() {
    cancelAnimationFrame(raf);
    root.hidden = true; root.classList.remove("in", "leave");
    document.documentElement.classList.remove("inst-open");
    if (s && s.onClose) s.onClose();
    s = null;
  }

  close.addEventListener("click", leave);
  root.addEventListener("keydown", (e) => { if (e.key === "Escape") leave(); });
  addEventListener("resize", () => { if (s) place(); });

  // the ground: blocks by the dither, over the page (q 0..16 of them in), or out to it
  function curtain(q) {
    const dpr = Math.min(devicePixelRatio || 1, 2), w = innerWidth, h = innerHeight;
    if (ground.width !== Math.round(w * dpr) || ground.height !== Math.round(h * dpr)) { ground.width = Math.round(w * dpr); ground.height = Math.round(h * dpr); }
    const g = ground.getContext("2d");
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, w, h);
    g.fillStyle = "#141416";
    for (let by = 0, Y = 0; Y < h; by++, Y += BLOCK)
      for (let bx = 0, X = 0; X < w; bx++, X += BLOCK)
        if (bay(bx, by) < q) g.fillRect(X, Y, BLOCK, BLOCK);
  }

  function paint(sp, now) {
    const { W, H, tone, at, edge, lit, data } = sp, d = data.data, quiet = calm();
    const t = now - s.phaseAt, done = s.phase === "done", fail = s.fail;
    // the arrangement: frame 0 while it arrives, stepping once it is all there (not for less motion)
    if (!quiet && now - lastStep > 110) { frame = (frame + 1 + ((Math.random() * 4) | 0)) % 6; lastStep = now; }
    const T = tone[(done || s.out || s.phase === "reboot") && !quiet ? frame : 0];
    // a pass of the deep blue while the FM-1 reads the package; out and back in blocks while it restarts; the glint
    const pass = !quiet && (s.phase === "verify" || s.phase === "start") ? ((now - s.t0) % 1600) / 1600 * 1.3 - 0.15 : -9;
    let gone = 0;
    if (s.phase === "loader" && !quiet) { const c = t % 2400; gone = c < 500 ? c / 500 * 16 : c < 900 ? 16 : c < 1400 ? (1400 - c) / 500 * 16 : 0; }
    const glint = !quiet && s.phase === "reboot" ? (t % 1100) / 1100 * 1.4 - 0.2 : -9;
    const p = s.shown;
    for (let y = 0, j = 0; y < H; y++)
      for (let x = 0; x < W; x++, j++) {
        const k = j * 4, want = T[j], base = tone[0][j];
        if (base < 0) { d[k + 3] = 0; continue; }
        // lit: arrived (in) or not yet gone (out)
        const on = s.out ? p < at[j] : p >= at[j];
        let rgb;
        if (on) {
          if (lit[j] < 0) lit[j] = now;
          let c = want < 0 ? base : want;
          if (!quiet && !s.out) { const age = now - lit[j]; if (age < HEAT) c = Math.min(c, Math.floor(age / HEAT * 8.5 - 1 + bay(x + 1, y + 2) / 16)); }
          if (glint > -9 && Math.abs(x / W - glint) < 0.05 + bay(x, y) / 16 * 0.04) c = 7;
          rgb = fail ? (edge[j] || bay(x, y) < 6 ? LABEL : LINE) : c < 0 ? DEEP : PAL[c];   // stopped: cooled
        } else {
          lit[j] = -1;
          rgb = edge[j] ? LINE : GRID;
          if (pass > -9 && edge[j] && Math.abs(x / W - pass) < 0.06 && bay(x, y) < 10) rgb = DEEP;
        }
        if (gone && bay(x >> 2, y >> 2) < gone) { d[k + 3] = 0; continue; }
        d[k] = rgb[0]; d[k + 1] = rgb[1]; d[k + 2] = rgb[2]; d[k + 3] = 255;
      }
    sp.g.putImageData(data, 0, 0);
    if (mark.width !== W || mark.height !== H) { mark.width = W; mark.height = H; mark.style.aspectRatio = `${W} / ${H}`; }
    mg.clearRect(0, 0, W, H);
    mg.drawImage(sp.cv, 0, 0);
  }

  function draw(now) {
    if (!s) return;
    raf = requestAnimationFrame(draw);
    if (now - last < 30) return;                     // 8-bit: about 30 frames a second, the page's MIDI first
    const dt = Math.min(1000, now - last);
    last = now;
    if (s.leaving >= 0) {
      const q = 16 - (now - s.leaving) / CURTAIN * 16;
      if (q <= 0) { end(); return; }
      curtain(q);
      return;
    }
    if (s.curtain < 1) {
      s.curtain = Math.min(1, (now - s.t0) / CURTAIN);
      curtain(s.curtain * 16);
      if (s.curtain >= 1) root.classList.add("in");
      return;
    }
    // the light runs a little behind the blocks and catches up (by time, however few frames the browser gives)
    s.shown += (s.prog - s.shown) * (calm() ? 1 : 1 - Math.exp(-dt / 160));
    if (Math.abs(s.prog - s.shown) < 0.002 || s.phase === "done") s.shown = s.prog;
    meterTo(s.shown);
    const n = narrow() ? "stack" : "wide";
    load(n).then((sp) => { if (s) paint(sp, now); }, () => {});
  }

  // the stage now: its letters lit, the glint's bits stepping as the page's own (app.js)
  setInterval(() => {
    const at = s && root.querySelector(".inst-st .at");
    if (at && !calm()) at.style.setProperty("--glint", `url(img/bits/glint_${(Math.random() * 6) | 0}.png)`);
  }, 140);

  return { open, step, finish, leave, isOpen: () => !!s };
}
