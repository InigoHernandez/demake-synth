// SPDX-License-Identifier: GPL-3.0-only
// The splash (index.html): the wordmark builds itself on the dark ground as the screen draws its light, 8-bit. Its
// bits arrive through the ordered dither, sweeping in; each heats from the deep blue to its own tone on the thermal
// ramp, the rim first and the pale core last; it holds while the six arrangements step and a meter of ticks fills
// under it; then the whole screen leaves in blocks by the same dither. Once a visit (the head's script decides), never
// when the reader asks for less motion; a key, a click, a touch or a scroll skips to the leaving.
(() => {
  "use strict";
  const root = document.documentElement, el = document.getElementById("splash");
  if (!el || !root.classList.contains("splashing")) { if (el) el.remove(); return; }
  const cv = el.querySelector("canvas"), g = cv.getContext("2d");
  const PAL = ["2459ff", "5a48f6", "8e3aea", "c02fd2", "e52bc2", "ff45c2", "ff8cda", "ffddf2"]
    .map((h) => [0, 2, 4].map((i) => parseInt(h.slice(i, i + 2), 16)));
  const DEEP = [16, 34, 154], GROUND = "#141416", LINE = "#2a2a2d", INK = "#ededea", T4 = "#e52bc2";
  const B = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5], bay = (x, y) => B[(y & 3) * 4 + (x & 3)];
  const ARRIVE = 650, HEAT0 = 150, HEAT1 = 1150, HOLD = 1750, LEAVE = 550, TICKS = 41, BLOCK = 16;
  const narrow = innerWidth < 800, img = new Image();
  let W = 0, H = 0, tone = [], off = null, oc = null, data = null, t0 = 0, frame = 0, lastStep = 0, done = false, skip = false;

  const end = () => {
    if (done) return;
    done = true;
    root.classList.remove("splashing");
    el.remove();
    ["keydown", "pointerdown", "wheel", "touchstart"].forEach((e) => removeEventListener(e, hurry));
  };
  const hurry = () => { skip = true; };
  ["keydown", "pointerdown", "wheel", "touchstart"].forEach((e) => addEventListener(e, hurry, { passive: true }));

  // each of the sprite's six frames as tones 0..7 (the ramp), -1 where nothing is lit
  img.onload = () => {
    W = img.naturalWidth; H = img.naturalHeight / 6;
    const c = document.createElement("canvas"); c.width = W; c.height = H * 6;
    const x = c.getContext("2d"); x.drawImage(img, 0, 0);
    const px = x.getImageData(0, 0, W, H * 6).data;
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
    off = document.createElement("canvas"); off.width = W; off.height = H;
    oc = off.getContext("2d"); data = oc.createImageData(W, H);
    requestAnimationFrame((now) => { t0 = now; draw(now); });
  };
  img.onerror = end;
  img.src = narrow ? "img/wordmark_stack.png" : "img/wordmark.png";

  function size() {
    const dpr = Math.min(devicePixelRatio || 1, 2), w = innerWidth, h = innerHeight;
    if (cv.width !== Math.round(w * dpr) || cv.height !== Math.round(h * dpr)) { cv.width = Math.round(w * dpr); cv.height = Math.round(h * dpr); }
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.imageSmoothingEnabled = false;
    return [w, h];
  }

  function draw(now) {
    if (done) return;
    let t = now - t0;
    if (skip && t < HOLD) { t0 = now - HOLD; t = HOLD; }
    if (t > HOLD + LEAVE) { end(); return; }
    const [w, h] = size();
    // the mark's cells: whole pixels, as big as the screen allows
    const s = Math.max(1, Math.floor(Math.min((narrow ? w - 32 : w * 0.8) / W, h * (narrow ? 0.6 : 0.42) / H)));
    const mw = W * s, mh = H * s, mx = Math.round((w - mw) / 2), my = Math.round((h - mh) / 2 - 24);
    // the light: arriving and heating, or holding, its arrangement stepping
    if (now - lastStep > 110) { frame = (frame + 1 + ((Math.random() * 4) | 0)) % 6; lastStep = now; }
    const p = Math.min(1, t / ARRIVE), heat = Math.min(1, Math.max(0, (t - HEAT0) / (HEAT1 - HEAT0)));
    const T = tone[t < HEAT1 ? 0 : frame], d = data.data;
    for (let y = 0, j = 0; y < H; y++)
      for (let x = 0; x < W; x++, j++) {
        const k = j * 4, want = T[j];
        if (want < 0 || p < bay(x, y) / 16 * 0.55 + x / W * 0.45) { d[k + 3] = 0; continue; }
        let c = want;
        if (heat < 1) { c = Math.min(want, Math.floor(heat * 8.5 - 1 + bay(x + 1, y + 2) / 16)); }
        const rgb = c < 0 ? DEEP : PAL[c];
        d[k] = rgb[0]; d[k + 1] = rgb[1]; d[k + 2] = rgb[2]; d[k + 3] = 255;
      }
    oc.putImageData(data, 0, 0);
    g.clearRect(0, 0, w, h);
    g.fillStyle = GROUND; g.fillRect(0, 0, w, h);
    g.drawImage(off, mx, my, mw, mh);
    // the meter: ticks in ink up to where it has reached, the tick at it tall and lit, as on the screen
    const at = Math.min(TICKS - 1, Math.floor(Math.min(1, t / HOLD) * TICKS)), ty = my + mh + 48, step = mw / (TICKS - 1);
    for (let i = 0; i < TICKS; i++) {
      const x = Math.round(mx + i * step) - (i === TICKS - 1 ? 1 : 0);
      if (i === at) { g.fillStyle = T4; g.fillRect(x, ty - 6, 2, 24); }
      else { g.fillStyle = i < at ? INK : LINE; g.fillRect(x, ty, 1, 12); }
    }
    // leaving: the screen goes in blocks, by the dither, to the page under it
    if (t > HOLD) {
      el.classList.add("leaving");
      const q = (t - HOLD) / LEAVE * 16;
      for (let by = 0, Y = 0; Y < h; by++, Y += BLOCK)
        for (let bx = 0, X = 0; X < w; bx++, X += BLOCK)
          if (bay(bx, by) < q) g.clearRect(X, Y, BLOCK, BLOCK);
    }
    requestAnimationFrame(draw);
  }
})();
