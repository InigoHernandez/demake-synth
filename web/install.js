// SPDX-License-Identifier: GPL-3.0-only
// Demake's installer (install.html): MONO2K's installer on Demake's page. Felucca's update protocol and package code
// (fm1/fm1ota.js, fm1/fm1pkg.js) do the work; this file says what is happening, in the page's words.
import { Updater } from "./fm1/fm1ota.js?v=2";
import { productOf, logicalImage, validateStockPackage, sha256hex } from "./fm1/fm1pkg.js?v=1";
import { installing } from "./installing.js?v=1";

const $ = (id) => document.getElementById(id);

/* what an identity says the FM-1 runs ("FM-1_015", "ota-FM-1_969", ...). Only Demake's own number is called Demake
 * (the FM-1 Firmware Hub's rule: 966-968, Demake's before 0.4, are other firmwares' now); the others by their number */
function describe(id) {
  const v = id.version;
  if (/^ota-/i.test(id.model)) return "Update mode: an update waits to be finished";
  if (id.model !== "FM-1") return `Another device (${id.text})`;
  if (v === 0) return "USB rescue mode";
  if (v === 15) return "M-VAVE's official firmware, V15";
  if (v < 100) return `M-VAVE's official firmware (${id.text})`;
  if (v === 969) return "Demake";
  if (v === 980) return "MONO2K";
  return `Another open firmware (${id.text})`;
}

let midi = null, busy = false, pkg = null, stock = null;
const M = window.dmk || { swap: (e, k, f) => f(), shake() {} };
/* the page's line: a new one swaps in, an error shakes; quiet (the meter's share, the check's count) writes at once */
const say = (t, cls, quiet) => {
  const line = $("say"), w = $("say-t"), key = t + "|" + (cls || "");
  const set = () => { w.textContent = t; line.className = "log" + (cls ? " " + cls : ""); if (cls === "err") M.shake(w); };
  if (!quiet) { M.swap(w, key, set); return; }
  w._key = key; w._apply = set;
  if (!w._out) set();
};
const put = (id, t) => { const e = $(id); M.swap(e, t, () => { e.textContent = t; }); };
/* the steps, a stepper: the one to do now lit, its action the step's one lit button (the other in outline), the
 * ones done checked; never back */
let reached = 0;
const lead = (b, on) => { b.classList.toggle("lit", on); b.classList.toggle("pill", !on); };
const stepAt = (n) => {
  reached = Math.max(reached, n);
  [$("s-connect"), $("s-install")].forEach((s, i) => { s.classList.toggle("done", reached > i); s.classList.toggle("now", reached === i); });
  lead($("connect"), reached === 0);
  lead($("install"), reached >= 1);
};
const lock = (on) => {
  busy = on;
  $("connect").disabled = on;
  $("install").disabled = on || !pkg;
  $("stock").disabled = on || !stock;
  $("file").disabled = on; $("stock-file").disabled = on;
};
window.addEventListener("beforeunload", (e) => { if (busy) { e.preventDefault(); e.returnValue = "Writing: keep this page open."; } });

async function access() {
  if (midi) return midi;
  if (!navigator.requestMIDIAccess) throw new Error("No Web MIDI here: use Chrome or Edge on a computer.");
  try { midi = await navigator.requestMIDIAccess({ sysex: true }); }
  catch (e) { throw new Error("MIDI was not allowed. Allow it in the site settings and try again."); }
  return midi;
}

function show(id) {
  const link = $("st-link"), on = id ? "on" : "no";
  M.swap(link, on, () => { link.textContent = id ? "Connected" : "Not found"; link.dataset.link = on; });
  put("st-runs", id ? describe(id) : "–");
  put("st-id", id ? id.text : "–");
}

$("connect").addEventListener("click", async () => {
  if (busy) return;
  lock(true);
  try {
    const up = new Updater(await access(), (m) => console.log(m));
    say("Asking the FM-1…");
    put("connect-sub", "Asking the FM-1…");
    const dev = await up.find();
    if (!dev) {
      show(null);
      put("connect-sub", "Not found: try again");
      say("No FM-1 answered. Check the cable, close DAWs and other tabs, try again.", "err");
      return;
    }
    dev.link.close();
    show(dev.id);
    put("connect-sub", `Connected · ${dev.id.text}`);
    stepAt(1);
    const what = describe(dev.id);
    if (/^ota-/i.test(dev.id.model)) say(`${what}. Press Install to finish it.`);
    else say(`It runs ${what}. ` + (pkg ? `Press Install for ${pkg.label}.` : "Choose a .fwsc in step 02, then Install."), "ok");
  } catch (e) { say(e.message, "err"); }
  finally { lock(false); }
});

/* a package: its identity, the open update loader in it, the image the FM-1 reads */
function takePackage(raw, label) {
  const product = productOf(raw);
  if (!/^FM-1_\d+$/.test(product)) throw new Error("not an FM-1 package");
  if (!new TextDecoder("latin1").decode(raw).includes("FELUCCA-LOADER-1"))
    throw new Error("no update loader this page can drive (M-VAVE's own? Use Back to V15)");
  pkg = { product, image: logicalImage(raw), label };
  put("install-sub", `${label} · ${product}`);
  $("install").disabled = busy;
}

(async () => {
  try {
    const r = await fetch("firmware/demake.json", { cache: "no-store" });
    if (!r.ok) throw new Error("none");
    const meta = await r.json();
    const b = await fetch("firmware/" + meta.file, { cache: "no-store" });
    if (!b.ok) throw new Error(`${meta.file}: HTTP ${b.status}`);
    const raw = new Uint8Array(await b.arrayBuffer());
    if (meta.sha256 && await sha256hex(raw) !== meta.sha256) throw new Error(`${meta.file} does not match its fingerprint`);
    if (productOf(raw) !== meta.product) throw new Error(`${meta.file} is not ${meta.product}`);
    takePackage(raw, `Demake ${meta.version}`);
    put("st-pkg", `${meta.version}, ${meta.date}`);
    put("pkg-note", `Ready to install: ${(raw.length / 1024).toFixed(0)} KB.`);
  } catch (e) {
    put("st-pkg", "Not published yet");
    put("pkg-note", e.message === "none"
      ? "A package is published here once it has run on an FM-1. Connect works already, and a test build installs from a file in step 02."
      : `The package could not be read: ${e.message}.`);
  }
})();

$("file").addEventListener("change", async (ev) => {
  const f = ev.target.files[0];
  if (!f) return;
  try {
    takePackage(new Uint8Array(await f.arrayBuffer()), f.name);
    put("pkg-note", `Ready: ${f.name}, identity ${pkg.product}. Press Install, then leave the cable in until it says done.`);
    say(`${f.name} is ready to install.`, "ok");
  } catch (e) { say(`${f.name}: ${e.message}`, "err"); }
});

const WORDS = {
  notfound: "No FM-1 answered. Check the cable, close other programs, try again.",
  model: "That package is for another device.",
  stopped: "The FM-1 stopped answering. Nothing was written: try again.",
  lost: "Disconnected. If it is in update mode, replug and press Install again.",
  noloader: "The update mode did not start. Replug and press Install again.",
  noreturn: "The FM-1 did not come back: switch it off and on.",
  mismatch: "Written, but the FM-1 reports another firmware",
  badreq: "The FM-1 asked for data outside the package: the wrong one?",
};

/* the progress: the page's meter, ticks in ink up to what is written, the tick at it lit */
const bar = $("bar"), TICKS = 41, tick = [];
for (let i = 0; i < TICKS; i++) tick.push(bar.appendChild(document.createElement("i")));
const progress = (p) => { const at = Math.round(p * (TICKS - 1) / 100); tick.forEach((t, i) => { t.className = i < at ? "on" : i === at ? "at" : ""; }); };

/* the installing screen over the page from the FM-1's first answer to the end (an error before it, such as no FM-1,
 * stays on the page's line); its words for the end, the page's line keeps them too */
const inst = installing();
const runs = (a) => { const id = { model: "FM-1", version: parseInt(a.split("_")[1], 10), text: a }; return describe(id); };

async function write(p, verb, look) {
  lock(true);
  bar.hidden = false; progress(0);
  $("status").scrollIntoView({ behavior: "smooth", block: "start" });
  const total = Math.ceil(p.image.length / 512);
  const step = (k, a) => {
    if (!inst.isOpen()) inst.open({ ...look, total });
    inst.step(k, a);
    if (k === "done") inst.finish(`Done: your FM-1 runs ${a === p.product && look.ran ? look.ran : runs(a)}.`, true);
    if (k === "start") { say(`Found ${a}. ${verb}: do not unplug.`); if (!look.out) stepAt(1); }
    else if (k === "verify") say(`Checking the package (${a})…`, "", true);
    else if (k === "loader") say("The FM-1 restarts into its update mode…");
    else if (k === "write") { const v = Math.min(99, Math.round(a * 100 / total)); progress(v); say(`Writing, ${v} %`, "", true); }
    else if (k === "reboot") { progress(100); say("Written. The FM-1 starts again…"); }
    else if (k === "done") { say(`Done: the FM-1 runs ${describe({ model: "FM-1", version: parseInt(a.split("_")[1], 10), text: a })}.`, "ok");
      if (!look.out) stepAt(2);
      show({ model: "FM-1", text: a, version: parseInt(a.split("_")[1], 10) }); }
  };
  try {
    const up = new Updater(await access(), (m) => console.log(m));
    if (await up.resume(p.image, step, p.product)) {
      say(`That finished an update cut short: the FM-1 runs ${p.product}.`, "ok");
      if (!look.out) stepAt(2);
      inst.finish(`That finished an update cut short: your FM-1 runs ${look.ran || runs(p.product)}.`, true);
      return;
    }
    await up.install(p.image, p.product, step);
  } catch (e) {
    const w = (WORDS[e.code] || e.message) + (e.detail ? ` (${e.detail})` : "");
    say(w, "err");
    if (inst.isOpen()) inst.finish(w, false);
    console.error(e);
  } finally { lock(false); }
}

$("install").addEventListener("click", () => {
  if (pkg && !busy) write(pkg, "Installing Demake", { verb: "Installing", what: pkg.label, ran: /^Demake /.test(pkg.label) ? pkg.label : "" });
});

$("stock-file").addEventListener("change", async (ev) => {
  const f = ev.target.files[0];
  stock = null; lock(false);
  if (!f) return;
  try {
    stock = await validateStockPackage(new Uint8Array(await f.arrayBuffer()));
    put("stock-sub", "Official V15, checked");
    say("The official V15 file checks out.", "ok");
  } catch (e) { put("stock-sub", "Not the official V15 file"); say(`${f.name}: ${e.message}`, "err"); }
  lock(false);
});

$("stock").addEventListener("click", () => {
  if (!stock || busy) return;
  if (!confirm("Back to M-VAVE's official V15? Demake, and what it keeps in the FM-1, will be gone.")) return;
  write(stock, "Going back to V15", { verb: "Going back", what: "M-VAVE's official V15", out: true, ran: "M-VAVE's official firmware, V15" });
});

/* the installing screen without an FM-1: install.html#preview (or #preview-stop, #preview-back), at about the pace
 * of a real install, for a look at it; it writes nothing and talks to no device */
async function preview(kind) {
  if (busy || inst.isOpen()) return;
  const wait = (ms) => new Promise((r) => setTimeout(r, ms)), out = kind === "back", total = 1193;
  inst.open(out ? { verb: "Going back", what: "M-VAVE's official V15 (a preview)", out, total }
                : { verb: "Installing", what: "Demake 0.4.25 (a preview)", total });
  inst.step("start", "FM-1_969"); await wait(1800);
  for (let n = 1; n <= 40; n++) { inst.step("verify", n); await wait(90); }
  inst.step("loader"); await wait(4200);
  for (let n = 1; n <= total; n++) {
    inst.step("write", n); await wait(22);
    if (kind === "stop" && n === 700) { inst.finish(WORDS.lost, false); return; }
  }
  inst.step("reboot"); await wait(3600);
  inst.finish(out ? "Done: your FM-1 runs M-VAVE's official firmware, V15." : "Done: your FM-1 runs Demake 0.4.25.", true);
}
const previewHash = () => { const m = /^#preview(?:-(stop|back))?$/.exec(location.hash); if (m) preview(m[1] || "in"); };
addEventListener("hashchange", previewHash);
previewHash();
