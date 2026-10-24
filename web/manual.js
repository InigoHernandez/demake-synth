// SPDX-License-Identifier: GPL-3.0-only
// The manual (manual.html), after MONO2K's: one page at a time, the one the address names (#chords), with its
// breadcrumb and its neighbours; the contents light the page you are on; on a phone they fold away once you pick.
// Every block is made whole rows of the 48 px module tall, so the page keeps to the grid's lines.
(() => {
  "use strict";
  const $ = (s, r = document) => r.querySelector(s);
  const $$ = (s, r = document) => Array.from(r.querySelectorAll(s));
  const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
  const arts = $$(".mdoc article"), side = $("#side"), pager = $("#pager"), M = 48;
  const narrow = () => matchMedia("(max-width: 799px)").matches;
  document.body.classList.remove("noscript-all");

  arts.forEach((a) => {                                 /* the breadcrumb: the manual, the group, the page */
    const crumb = document.createElement("p");
    crumb.className = "crumb";
    crumb.innerHTML = `Manual <i>/</i> ${esc(a.dataset.group)} <i>/</i> ${esc($("h1", a).textContent)}`;
    a.insertBefore(crumb, a.firstChild);
  });

  /* whole rows: each block of the page shown, its figures, the contents and the pager, rounded up to the module
   * (inner ones first, so a block grows round the figures it holds) */
  function rows() {
    const on = $(".mdoc article.on") || document.body;
    const blocks = [...$$(":scope > *, .mfigs > .mfig", on), side, pager];
    blocks.forEach((el) => { if (el) el.style.minHeight = ""; });
    blocks.reverse().forEach((el) => { if (el && el.offsetHeight) el.style.minHeight = Math.ceil(el.offsetHeight / M - 0.01) * M + "px"; });
  }

  function show(id, scroll) {
    const i = Math.max(0, arts.findIndex((a) => a.id === id));
    arts.forEach((a, j) => a.classList.toggle("on", j === i));
    $$("a", side).forEach((l) => { if (l.getAttribute("href") === "#" + arts[i].id) l.setAttribute("aria-current", "page"); else l.removeAttribute("aria-current"); });
    const link = (a, cls, what) => a ? `<a class="${cls}" href="#${a.id}"><small>${what}</small><b>${esc($("h1", a).textContent)}</b></a>` : "<span></span>";
    pager.innerHTML = link(arts[i - 1], "prev", "Previous") + link(arts[i + 1], "next", "Next");
    document.title = `${$("h1", arts[i]).textContent} · Demake manual`;
    if (narrow()) side.open = false;
    rows();
    if (scroll) window.scrollTo({ top: Math.max(0, $(".mdocs").offsetTop - M) });
  }
  if ("scrollRestoration" in history) history.scrollRestoration = "manual";
  /* an address naming a page starts the page at the top, not at it: the browser jumps to the anchor once loaded */
  const top = () => window.scrollTo({ top: 0 });
  requestAnimationFrame(top);
  window.addEventListener("load", () => { top(); setTimeout(top, 0); });
  /* the articles merged since (the owner: fewer titles): an old link lands where its words went */
  const MOVED = { emulator: "introduction", rules: "panel", writing: "chords", patterns: "songs", song: "songs",
                  songmode: "songs", spaces: "effects", lfo: "filter", next: "credits" };
  const fromHash = (scroll) => { const h = decodeURIComponent(location.hash.slice(1)); show(MOVED[h] || h || "introduction", scroll); };
  window.addEventListener("hashchange", () => fromHash(true));
  fromHash(false);
  side.open = !narrow();
  side.addEventListener("toggle", rows);
  $("summary", side).addEventListener("click", (e) => { if (!narrow()) e.preventDefault(); });   /* folds on a phone only */
  const wide = () => { if (!narrow()) side.open = true; rows(); };
  window.addEventListener("resize", wide);
  matchMedia("(max-width: 799px)").addEventListener("change", wide);   /* a width change that sends no resize */
  $$("img", $(".mdoc")).forEach((im) => { if (!im.complete) im.addEventListener("load", rows); });
  if (document.fonts) document.fonts.ready.then(rows);
})();
