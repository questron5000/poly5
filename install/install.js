// SPDX-License-Identifier: GPL-3.0-only
// POLY5's install page: the steps and what they say. The update itself is Felucca's web installer (fm1ota.js,
// fm1pkg.js: Leo Kuroshita, Hügelton Instruments), unchanged: the same protocol as tools/fm1_install.py.
import { Updater, OUR_LOADER } from "./fm1ota.js";
import { productOf, logicalImage, sha256hex } from "./fm1pkg.js";

const PKG = { file: "poly5-1.0.fwsc", version: "1.0", product: "FM-1_945",
              sha256: "d7b1b63af283a95c32d02d5784187adfb401fb0607b4d854b8c3bdc4f4bf8936" };
const DEMO = new URLSearchParams(location.search).has("demo");   // the page without a device: for looking at it only
const $ = (id) => document.getElementById(id);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// what an identity is called here
function nameOf(text) {
  if (/^ota-/i.test(text)) return "an FM-1 in update mode";
  if (text === PKG.product) return "POLY5";
  if (text === "FM-1_015") return "the official firmware V15";
  if (/^FM-1_0\d\d$/.test(text)) return "the official firmware";
  if (/^FM-1_99\d$/.test(text)) return "Dinghy";
  return "another firmware";
}

const ERR = {
  notfound: "No FM-1 answered. Check the USB cable (one that carries data), that the FM-1 is switched on, and that no other app or browser tab is using it. Then try again.",
  model: "This device is not an FM-1, so nothing was written.",
  stopped: "The FM-1 stopped answering before anything was written. Switch it off and on, and try again.",
  lost: "The connection was lost. Plug the cable in again and press Install: an update that was cut off carries on where it stopped.",
  noloader: "The FM-1 went into update mode but did not show up again. Unplug the cable, plug it in, and press Install: it stays in update mode until the update is finished.",
  noreturn: "The update was written, but the FM-1 did not come back by itself. Switch it off and on.",
  mismatch: "The update was written, but the FM-1 reports another firmware than POLY5. Switch it off and on, then test the connection again.",
  badreq: "The FM-1 asked for something outside the package, so the update was stopped. Switch it off, hold OCT− while switching it on, and try again.",
  foreign: "The FM-1 is in the update mode of another firmware. Finish that update with its own installer first, or use the FM-1 Firmware Hub.",
  nomidi: "The browser did not allow MIDI. Allow it for this page (the lock or tune icon next to the address) and try again.",
  package: "The firmware file did not arrive whole, so nothing was sent to the FM-1. Reload the page and try again.",
};
function say(el, code, detail) {
  el.textContent = (ERR[code] || "Something went wrong: " + code) + (detail ? " " + detail : "");
  el.hidden = false;
}

// ---- the steps: one open at a time
const STEPS = ["browser", "ready", "connect", "install", "done"];
function open(name) {
  const at = STEPS.indexOf(name);
  STEPS.forEach((s, i) => {
    const el = $("step-" + s);
    el.classList.toggle("is-open", i === at);
    el.classList.toggle("is-done", i < at);
    el.querySelector(".st-body").hidden = i !== at;
  });
  $("step-" + name).scrollIntoView({ block: "nearest", behavior: "smooth" });
}

let access = null, updater = null, image = null, busy = false, found = null;

// ---- 1: the browser
function checkBrowser() {
  const ok = DEMO || typeof navigator.requestMIDIAccess === "function";
  $("browser-ok").hidden = !ok;
  $("browser-no").hidden = ok;
  $("to-ready").disabled = !ok;
}
$("to-ready").onclick = () => open("ready");

// ---- 2: get ready
$("to-connect").onclick = () => { open("connect"); connect(); };

// ---- 3: test the connection
async function midi() {
  if (access) return access;
  access = await navigator.requestMIDIAccess({ sysex: true });
  updater = new Updater(access);
  return access;
}
async function connect() {
  const out = $("connect-out"), err = $("connect-err"), go = $("to-install"), again = $("connect-again");
  err.hidden = true; go.hidden = true; again.hidden = true;
  out.textContent = "Looking for your FM‑1…";
  try {
    if (DEMO) { await sleep(900); found = { text: "FM-1_015" }; }
    else {
      try { await midi(); } catch (_) { out.textContent = ""; say(err, "nomidi"); again.hidden = false; return; }
      const dev = await updater.find(null, false) || await updater.find((id) => /^ota-/i.test(id.text), true);
      if (!dev) { out.textContent = ""; say(err, "notfound", updater.seen()); again.hidden = false; return; }
      dev.link.close();
      found = dev.id;
    }
  } catch (e) { out.textContent = ""; say(err, e.code || "notfound", e.detail); again.hidden = false; return; }
  const ota = /^ota-/i.test(found.text);
  if (ota && !DEMO && !OUR_LOADER(found)) { out.textContent = ""; say(err, "foreign", "(" + found.text + ")"); again.hidden = false; return; }
  out.textContent = ota
    ? "Found: an FM‑1 waiting in update mode. Install will finish the update."
    : "Found your FM‑1. It is running " + nameOf(found.text) + " (" + found.text + ").";
  $("install-from").textContent = ota ? "An update is waiting to be finished." :
    found.text === PKG.product ? "POLY5 is already on it: installing again puts version " + PKG.version + " on it and keeps your patches." :
    "It will replace " + nameOf(found.text) + ". Saves made with that firmware may be lost.";
  go.hidden = false;
}
$("connect-again").onclick = connect;
$("to-install").onclick = () => { open("install"); fetchPackage(); };

// ---- 4: install
async function fetchPackage() {
  const st = $("pkg-state"), btn = $("install-go");
  if (image) return;
  btn.disabled = true;
  st.textContent = "Getting the firmware file…";
  try {
    const r = await fetch(PKG.file, { cache: "no-store" });
    if (!r.ok) throw new Error("http " + r.status);
    const raw = new Uint8Array(await r.arrayBuffer());
    if (await sha256hex(raw) !== PKG.sha256 || productOf(raw) !== PKG.product) throw new Error("checksum");
    image = logicalImage(raw);
    st.textContent = "POLY5 " + PKG.version + " with its 24 patches, " + Math.round(raw.length / 1024) + " KB, checked: it is the file of the release.";
    ready();
  } catch (_) { st.textContent = ""; say($("install-err"), "package"); }
}
function ready() { $("install-go").disabled = busy || !image || !$("agree").checked; }
$("agree").onchange = ready;
function progress(label, pct, warn) {
  $("prog").hidden = false;
  $("prog-label").textContent = label;
  $("prog-bar").style.width = pct + "%";
  $("prog-bar").parentElement.setAttribute("aria-valuenow", String(Math.round(pct)));
  $("prog-warn").hidden = !warn;
}
function onStep(total) {
  return (what, n) => {
    if (what === "start") progress("Starting the update…", 2, false);
    else if (what === "verify") progress("The FM‑1 is checking the package…", 4, false);
    else if (what === "loader") progress("Switching to update mode…", 6, true);
    else if (what === "write") { const p = Math.min(99, Math.floor(n * 100 / total)); progress("Writing " + p + "%", 6 + p * 0.9, true); }
    else if (what === "reboot") progress("Restarting…", 97, true);
    else if (what === "done") progress("Done", 100, false);
  };
}
async function install() {
  const err = $("install-err"), btn = $("install-go");
  if (busy || !image) return;
  if (!$("agree").checked) return;
  busy = true; btn.disabled = true; $("agree").disabled = true; err.hidden = true;
  const total = Math.ceil(image.length / 512), step = onStep(total);
  try {
    if (DEMO) {
      step("start"); await sleep(700); step("verify"); await sleep(700); step("loader"); await sleep(900);
      for (let n = 0; n <= total; n += Math.ceil(total / 40)) { step("write", n); await sleep(80); }
      step("reboot"); await sleep(900); step("done");
    } else {
      // an FM-1 already in update mode (an install that was cut off): finish that write; else the whole install
      const resumed = await updater.resume(image, step, { product: PKG.product });
      if (!resumed) await updater.install(image, PKG.product, step);
    }
    busy = false;
    open("done");
  } catch (e) {
    busy = false; btn.disabled = false; $("agree").disabled = false;
    btn.textContent = "Try again";
    $("prog-warn").hidden = true;
    say(err, e.code || "stopped", e.code ? "" : String(e.message || e));
  }
}
$("install-go").onclick = install;
addEventListener("beforeunload", (e) => { if (busy) { e.preventDefault(); e.returnValue = ""; } });

if (DEMO) $("demo-note").hidden = false;
checkBrowser();
open("browser");
