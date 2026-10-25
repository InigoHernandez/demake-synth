// SPDX-License-Identifier: GPL-3.0-only
// Demake's installer (install.html): MONO2K's installer on Demake's page. Felucca's update protocol and package code
// (fm1/fm1ota.js, fm1/fm1pkg.js) do the work; this file says what is happening, in the page's words.
import { Updater } from "./fm1/fm1ota.js?v=2";
import { productOf, logicalImage, validateStockPackage, sha256hex } from "./fm1/fm1pkg.js?v=1";

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
const say = (t, cls) => { const s = $("say"); s.textContent = t; s.className = "log" + (cls ? " " + cls : ""); };
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
  $("st-link").textContent = id ? "Connected" : "Not found";
  $("st-runs").textContent = id ? describe(id) : "–";
  $("st-id").textContent = id ? id.text : "–";
}

$("connect").addEventListener("click", async () => {
  if (busy) return;
  lock(true);
  try {
    const up = new Updater(await access(), (m) => console.log(m));
    say("Asking the FM-1…");
    $("connect-sub").textContent = "Asking the FM-1…";
    const dev = await up.find();
    if (!dev) {
      show(null);
      $("connect-sub").textContent = "Not found: try again";
      say("No FM-1 answered. Check the cable, close DAWs and other tabs, try again.", "err");
      return;
    }
    dev.link.close();
    show(dev.id);
    $("connect-sub").textContent = `Connected · ${dev.id.text}`;
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
  $("install-sub").textContent = `${label} · ${product}`;
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
    $("st-pkg").textContent = `${meta.version}, ${meta.date}`;
    $("pkg-note").textContent = `Ready to install: ${(raw.length / 1024).toFixed(0)} KB.`;
  } catch (e) {
    $("st-pkg").textContent = "Not published yet";
    $("pkg-note").textContent = e.message === "none"
      ? "A package is published here once it has run on an FM-1. Connect works already, and a test build installs from a file in step 02."
      : `The package could not be read: ${e.message}.`;
  }
})();

$("file").addEventListener("change", async (ev) => {
  const f = ev.target.files[0];
  if (!f) return;
  try {
    takePackage(new Uint8Array(await f.arrayBuffer()), f.name);
    $("pkg-note").textContent = `Ready: ${f.name}, identity ${pkg.product}. Press Install, then leave the cable in until it says done.`;
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

async function write(p, verb) {
  lock(true);
  bar.hidden = false; progress(0);
  $("status").scrollIntoView({ behavior: "smooth", block: "start" });
  const total = Math.ceil(p.image.length / 512);
  const step = (k, a) => {
    if (k === "start") say(`Found ${a}. ${verb}: do not unplug.`);
    else if (k === "verify") say(`Checking the package (${a})…`);
    else if (k === "loader") say("The FM-1 restarts into its update mode…");
    else if (k === "write") { const v = Math.min(99, Math.round(a * 100 / total)); progress(v); say(`Writing, ${v} %`); }
    else if (k === "reboot") { progress(100); say("Written. The FM-1 starts again…"); }
    else if (k === "done") { say(`Done: the FM-1 runs ${describe({ model: "FM-1", version: parseInt(a.split("_")[1], 10), text: a })}.`, "ok");
      show({ model: "FM-1", text: a, version: parseInt(a.split("_")[1], 10) }); }
  };
  try {
    const up = new Updater(await access(), (m) => console.log(m));
    if (await up.resume(p.image, step, p.product)) { say(`That finished an update cut short: the FM-1 runs ${p.product}.`, "ok"); return; }
    await up.install(p.image, p.product, step);
  } catch (e) {
    say((WORDS[e.code] || e.message) + (e.detail ? ` (${e.detail})` : ""), "err");
    console.error(e);
  } finally { lock(false); }
}

$("install").addEventListener("click", () => { if (pkg && !busy) write(pkg, "Installing Demake"); });

$("stock-file").addEventListener("change", async (ev) => {
  const f = ev.target.files[0];
  stock = null; lock(false);
  if (!f) return;
  try {
    stock = await validateStockPackage(new Uint8Array(await f.arrayBuffer()));
    $("stock-sub").textContent = "Official V15, checked";
    say("The official V15 file checks out.", "ok");
  } catch (e) { $("stock-sub").textContent = "Not the official V15 file"; say(`${f.name}: ${e.message}`, "err"); }
  lock(false);
});

$("stock").addEventListener("click", () => {
  if (!stock || busy) return;
  if (!confirm("Back to M-VAVE's official V15? Demake, and what it keeps in the FM-1, will be gone.")) return;
  write(stock, "Going back to V15");
});
