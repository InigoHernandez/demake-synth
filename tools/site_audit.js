// SPDX-License-Identifier: GPL-3.0-only
// The site's layout audit, run in the browser on a page of web/ (paste it into the console, or a preview tool's
// script runner) at 24, 16 and 7 modules. It returns what breaks the rules (docs/plan.md, 6):
//   text     two lines of text from different elements that overlap, or sit less than 4 px apart on a line
//   cell     a line of text past the bottom or the right of its grid cell
//   box      a line of text over a box it does not belong to (a button, a pill, a card, a key)
//   offgrid  a grid item whose corner is not on the 48 px module's lines
//   gaps     each section's rows, then the air above its content and below it, in modules (the rhythm: 1 below)
(() => {
  const cv = document.querySelector(".canvas");
  const shown = (el) => !el.checkVisibility || el.checkVisibility();   /* not inside a folded details */
  const vis = (el) => !el.closest("[hidden]") && !el.closest("svg") && shown(el) && getComputedStyle(el).display !== "none";
  const lines = [];
  const walker = document.createTreeWalker(cv, NodeFilter.SHOW_TEXT);
  for (let n; (n = walker.nextNode());) {
    if (!n.textContent.trim() || !vis(n.parentElement)) continue;
    const rg = document.createRange(); rg.selectNodeContents(n);
    for (const r of rg.getClientRects()) if (r.width > 0.5 && r.height > 0.5) lines.push({ el: n.parentElement, r, t: n.textContent.trim().slice(0, 30) });
  }
  const meet = (a, b, d) => Math.min(a.right, b.right) - Math.max(a.left, b.left) > 1 && Math.min(a.bottom, b.bottom) - Math.max(a.top, b.top) > d;
  // two texts on one line closer than 4 px (the owner: "How it works" ran into "Over USB", a pixel apart)
  const near = (a, b) => Math.min(a.bottom, b.bottom) - Math.max(a.top, b.top) > 2 && Math.max(a.left, b.left) - Math.min(a.right, b.right) < 4;
  const block = (el) => { let e = el; while (e && getComputedStyle(e).display.startsWith("inline")) e = e.parentElement; return e; };   // (words of one sentence: a space apart)
  const cellOf = (el) => { let e = el; while (e && e.parentElement && getComputedStyle(e.parentElement).display !== "grid") e = e.parentElement; return e; };
  const boxes = [...cv.querySelectorAll(".page, .lit, .pill, .card, kbd, .tiles i, .emu, .eng .img")].filter(vis).map((b) => ({ b, r: b.getBoundingClientRect() }));
  const res = { width: innerWidth, scroll: document.documentElement.scrollWidth > innerWidth, text: [], cell: [], box: [] };
  for (let i = 0; i < lines.length; i++) for (let j = i + 1; j < lines.length; j++) {
    const a = lines[i], b = lines[j];
    if (a.el !== b.el && !a.el.matches(".big") && !b.el.matches(".big") && (meet(a.r, b.r, 2) || (near(a.r, b.r) && block(a.el) !== block(b.el)))) res.text.push([a.t, b.t]);
  }
  for (const L of lines) {
    const c = cellOf(L.el);                             // a statement's glyph box runs past its 48 px lines: not a fault
    if (c && !c.matches(".say")) { const ir = c.getBoundingClientRect(); if (L.r.bottom > ir.bottom + 5 || L.r.right > ir.right + 1) res.cell.push([L.t, String(c.className || c.tagName)]); }
    for (const B of boxes) if (!B.b.contains(L.el) && meet(L.r, B.r, 1)) res.box.push([L.t, String(B.b.className || B.b.tagName)]);
  }
  const ext = (el) => {
    const rs = [], w = document.createTreeWalker(el, NodeFilter.SHOW_TEXT);
    for (let m; (m = w.nextNode());) { if (!m.textContent.trim() || !vis(m.parentElement)) continue; const rg = document.createRange(); rg.selectNodeContents(m); for (const r of rg.getClientRects()) if (r.height > 1) rs.push(r); }
    const b = el.getBoundingClientRect();
    if (el.matches(".card, .emu, .lit, .pill, .page, .tiles, .eng, .main, .bits, .ticks, .act, .log, ol, .rule") || el.querySelector(".card, .img, img, canvas, .page") || !rs.length) return [b.top, b.bottom];
    return [Math.min(...rs.map((r) => r.top)), Math.max(...rs.map((r) => r.bottom))];
  };
  res.gaps = [...cv.querySelectorAll(":scope > .g:not(.strip)")].map((s) => {
    const sr = s.getBoundingClientRect(), ex = [...s.children].filter(vis).map(ext);
    if (!ex.length) return "";
    const top = Math.min(...ex.map((e) => e[0])), bot = Math.max(...ex.map((e) => e[1]));
    return `${s.className.replace("g ", "")} ${sr.height / 48}: ${((top - sr.top) / 48).toFixed(1)}/${((sr.bottom - bot) / 48).toFixed(1)}`;
  }).filter(Boolean).join(" | ");
  const c = cv.getBoundingClientRect(), on = (v) => Math.abs(v / 48 - Math.round(v / 48)) < 0.01;
  res.offgrid = [...cv.querySelectorAll(".g > *")].filter(vis).filter((e) => { const r = e.getBoundingClientRect(); return !on(r.left - c.left) || !on(r.top - c.top); }).map((e) => String(e.className || e.tagName));
  return res;
})();
