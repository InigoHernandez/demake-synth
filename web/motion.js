// SPDX-License-Identifier: GPL-3.0-only
// The site's motion, on transitions.dev's scale (the tokens in style.css, chosen by what each motion does):
//   swap      a line that changes leaves 4 px up through a blur and the new one comes in from under (text states swap)
//   shake     an error's words shake once (error state shake)
//   tabs      a list of pages gets one light behind the page shown, which glides to the next (sliding tabs)
//   reveal    a section coming into view: its rule drawn, its parts rising a stagger apart (texts reveal)
//   dissolve  a screen dissolves into the next through the screen's own ordered dither (Libraries.dev's image loader,
//             a mosaic that settles into the picture, in Demake's 8-bit)
// When the reader asks for less motion html.motion is never set and each helper only writes its change.
(() => {
  "use strict";
  const root = document.documentElement;
  const calm = matchMedia("(prefers-reduced-motion: reduce)").matches;
  if (!calm) root.classList.add("motion");
  const moving = () => root.classList.contains("motion");
  const ms = (token) => parseFloat(getComputedStyle(root).getPropertyValue(token)) || 0;
  const ease = (p) => 1 - Math.pow(1 - p, 5);          /* --ease-smooth-out's curve, near enough, for what JS draws */
  const STAGGERED = 8;                                  /* a section's parts that wait their turn: the last within 300 ms */
  const BAYER = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5];

  /* ---- a line that changes. key names what it will say (the same again: nothing moves); apply writes it. Calls while
   * the old line is leaving only change what comes in */
  function swap(el, key, apply) {
    if (!el) return;
    if (el._key === key) return;
    el._key = key;
    if (!moving()) { apply(); return; }
    el.classList.add("sw");
    el._apply = apply;
    if (el._out) return;
    el._out = true;
    el.classList.add("sw-out");
    setTimeout(() => {
      el._out = false;
      el._apply();
      el.classList.replace("sw-out", "sw-in");
      void el.offsetWidth;                              /* (from under, at once; then home with the transition) */
      el.classList.remove("sw-in");
    }, ms("--duration-quick"));
  }

  function shake(el) {
    if (!el || !moving()) return;
    el.classList.remove("shake");
    void el.offsetWidth;
    el.classList.add("shake");
    el.addEventListener("animationend", () => el.classList.remove("shake"), { once: true });
  }

  /* ---- sliding tabs: the light is one element behind the buttons, placed on the one lit (.on) whenever that changes;
   * placed at once on a resize. Its ring's bits step as every lit thing's do */
  const slides = [];
  function tabs(list) {
    if (!list || list.querySelector(":scope > .slide")) return;
    const slide = document.createElement("i");
    slide.className = "slide snap";
    slide.setAttribute("aria-hidden", "true");
    list.prepend(slide);
    list.classList.add("tabs");
    slides.push(slide);
    const place = (snap) => {
      const on = list.querySelector(":scope > .page.on");
      slide.hidden = !on;
      if (!on) return;
      if (snap) slide.classList.add("snap");
      slide.style.width = on.offsetWidth + "px";
      slide.style.height = on.offsetHeight + "px";
      slide.style.transform = `translate(${on.offsetLeft}px, ${on.offsetTop}px)`;
      if (snap) { void slide.offsetWidth; slide.classList.remove("snap"); }
    };
    new MutationObserver(() => place(false)).observe(list, { subtree: true, attributes: true, attributeFilter: ["class"] });
    new ResizeObserver(() => place(true)).observe(list);
    place(true);
  }
  setInterval(() => {
    if (!moving() || document.hidden) return;
    const k = () => (Math.random() * 6) | 0;
    for (const s of slides) if (Math.random() < 0.5) s.style.setProperty("--ring24", `url(img/bits/ring24_${k()}.png)`);
    for (const n of document.querySelectorAll(".strip.now .nn")) if (Math.random() < 0.5) n.style.setProperty("--glint", `url(img/bits/glint_${k()}.png)`);
  }, 140);                                              /* (the screen's frame, as app.js's clock) */

  /* ---- a section coming into view. What is above the view already stays at rest; once played, the classes go, so
   * nothing of the reveal is left on the page */
  function reveal() {
    if (!moving() || !("IntersectionObserver" in window)) return;
    const settle = (s) => setTimeout(() => {
      s.classList.remove("rv", "in");
      for (const c of s.children) c.style.removeProperty("--i");
    }, ms("--duration-very-slow") + STAGGERED * ms("--duration-stagger"));
    const io = new IntersectionObserver((es) => es.forEach((e) => {
      if (!e.isIntersecting) return;
      io.unobserve(e.target);
      e.target.classList.add("in");
      settle(e.target);
    }), { rootMargin: "0px 0px -8% 0px" });
    for (const s of document.querySelectorAll(".canvas > .g:not(.head)")) {
      if (s.getBoundingClientRect().bottom < 0) continue;
      [...s.children].forEach((c, i) => c.style.setProperty("--i", Math.min(i, STAGGERED - 1)));
      s.classList.add("rv");
      io.observe(s);
    }
  }
  /* (after the splash: the hero's words rise once it has left) */
  const begin = () => root.classList.contains("splashing") ? setTimeout(begin, 100) : reveal();
  begin();

  /* ---- a picture into the next on a canvas of its size: both drawn, each pixel taking the new one as the dither's
   * threshold passes it, over --duration-slow. cell: the picture's pixels a screen pixel (the screens are drawn at 2x).
   * start() runs once both are read and the first frame is drawn; the promise says whether it ran to the end (false:
   * a newer one on the same canvas took over) */
  /* a picture loaded (decode() would wait for the page to be shown, in a tab behind) */
  const loaded = (im) => im.complete && im.naturalWidth ? Promise.resolve() : new Promise((ok, no) => {
    im.addEventListener("load", ok, { once: true });
    im.addEventListener("error", no, { once: true });
  });
  function mix(cv, from, to, cell = 1, start = () => {}) {
    const my = (cv._n = (cv._n || 0) + 1);
    return Promise.all([loaded(from), loaded(to)]).then(() => new Promise((done) => {
      if (my !== cv._n) { done(false); return; }
      const w = cv.width, h = cv.height, c = cv.getContext("2d", { willReadFrequently: true });
      c.drawImage(from, 0, 0, w, h);
      const a = c.getImageData(0, 0, w, h).data;
      c.drawImage(to, 0, 0, w, h);
      const b = c.getImageData(0, 0, w, h).data;
      const out = c.createImageData(w, h), o = out.data, th = new Uint8Array(w * h);
      for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) th[y * w + x] = BAYER[(((y / cell) | 0) & 3) * 4 + (((x / cell) | 0) & 3)];
      const t0 = performance.now(), D = ms("--duration-slow");
      const frame = (now) => {
        if (my !== cv._n) { done(false); return; }
        const p = Math.min(1, (now - t0) / D), lim = ease(p) * 16;
        for (let j = 0, q = 0; j < th.length; j++, q += 4) {
          const s = th[j] < lim ? b : a;
          o[q] = s[q]; o[q + 1] = s[q + 1]; o[q + 2] = s[q + 2]; o[q + 3] = 255;
        }
        c.putImageData(out, 0, 0);
        if (p < 1) requestAnimationFrame(frame); else done(true);
      };
      frame(t0);
      start();
      requestAnimationFrame(frame);
    })).catch(() => null);                            /* (a picture that would not load: null) */
  }

  /* an <img> into another picture: a canvas over it while the dither runs, the <img> already the new one under it */
  function dissolve(img, src, cell = 1) {
    if (!img || img.getAttribute("src") === src) return;
    if (!moving() || img.hidden || !img.complete || !img.naturalWidth) { img.src = src; return; }
    const cv = img._dv || (img._dv = Object.assign(document.createElement("canvas"), { className: "dissolve" }));
    cv.setAttribute("aria-hidden", "true");
    cv.width = img.naturalWidth; cv.height = img.naturalHeight;
    const next = new Image();
    next.src = src;
    mix(cv, img, next, cell, () => { img.after(cv); img.src = src; }).then((r) => {
      if (r) cv.remove();
      else if (r === null) { cv.remove(); img.src = src; }
    });
  }

  window.dmk = { swap, shake, tabs, dissolve, mix, moving, ms };
})();
