// SPDX-License-Identifier: GPL-3.0-only
// POLY5's install page: the steps and what they say. The update itself is Felucca's web installer (fm1ota.js,
// fm1pkg.js: Leo Kuroshita, Hügelton Instruments), unchanged: the same protocol as tools/fm1_install.py.
import { Updater, OUR_LOADER } from "./fm1ota.js";
import { productOf, logicalImage, sha256hex } from "./fm1pkg.js";

const PKG = { file: "poly5-1.1.fwsc", version: "1.1", product: "FM-1_945",
              sha256: "777d573c2a6b5a715d81074572247f8febec895aa148b3fa9e1cb70a29e3fcdc" };
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
  return "another custom firmware";
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
  nomidi: "The browser did not allow MIDI. Allow it for this page (the icon next to the address) and try again.",
  package: "The firmware file did not arrive whole, so nothing was sent to the FM-1. Reload the page and try again.",
};
function say(el, code, detail) {
  el.textContent = (ERR[code] || "Something went wrong: " + code) + (detail ? " " + detail : "");
  el.hidden = false;
}

// ---- the FM-1's screen beside the steps: what the real one shows at each moment
const SEGS = 20;
$("dev-segs").innerHTML = "<i></i>".repeat(SEGS);
function screen(state, cap, what, sub, pct) {
  $("dev-screen").dataset.state = state;
  $("dev-cap").textContent = cap;
  if (what !== undefined) $("dev-what").textContent = what;
  if (sub !== undefined) $("dev-sub").textContent = sub;
  const lit = pct == null ? 0 : Math.round(pct * SEGS / 100);
  [...$("dev-segs").children].forEach((s, i) => s.classList.toggle("on", i < lit));
  $("dev-pct").textContent = pct == null ? "" : Math.round(pct) + "%";
  if (state === "done") { const v = $("dev-boot"); v.currentTime = 0; v.loop = true; v.play().catch(() => {}); }
}

// ---- the steps: one at a time
const STEPS = ["browser", "ready", "connect", "install", "done"];
function open(name) {
  const at = STEPS.indexOf(name);
  STEPS.forEach((s, i) => {
    $("step-" + s).hidden = i !== at;
    $("tab-" + s).classList.toggle("is-open", i === at);
    $("tab-" + s).classList.toggle("is-done", i < at);
    if (i === at) $("tab-" + s).setAttribute("aria-current", "step"); else $("tab-" + s).removeAttribute("aria-current");
  });
}
document.querySelectorAll("[data-back]").forEach((b) => { b.onclick = () => { if (!busy) open(b.dataset.back); }; });

let access = null, updater = null, image = null, busy = false, found = null;

// ---- 1: the browser
function checkBrowser() {
  const ua = navigator.userAgent;
  const mine = /Edg\//.test(ua) ? "edge" : /Firefox\//.test(ua) ? "firefox" : /Chrome\//.test(ua) ? "chrome" : /Safari\//.test(ua) ? "safari" : "";
  const li = document.querySelector('.browsers [data-b="' + mine + '"]');
  if (li) li.classList.add("is-yours");
  const ok = DEMO || typeof navigator.requestMIDIAccess === "function";
  $("browser-h").textContent = ok ? "This browser can install POLY5" : "This browser can't install POLY5";
  $("browser-ok").hidden = !ok;
  $("browser-no").hidden = ok;
  $("to-ready").disabled = !ok;
}
$("to-ready").onclick = () => open("ready");

// ---- 2: get ready: every line ticked
const checks = [...document.querySelectorAll("#checks input")];
checks.forEach((c) => { c.onchange = () => { $("to-connect").disabled = !checks.every((x) => x.checked); }; });
$("to-connect").onclick = () => { open("connect"); connect(); };

// ---- 3: test the connection
async function midi() {
  if (access) return access;
  access = await navigator.requestMIDIAccess({ sysex: true });
  updater = new Updater(access);
  return access;
}
async function connect() {
  const out = $("connect-out"), err = $("connect-err"), go = $("to-install"), again = $("connect-again"), h = $("connect-h");
  const fail = (code, detail) => { h.textContent = "No FM‑1 found"; out.textContent = ""; say(err, code, detail); again.hidden = false; $("conn").dataset.state = "bad"; screen("idle", "Not connected"); };
  err.hidden = true; go.hidden = true; again.hidden = true; $("connect-ask").hidden = false;
  h.textContent = "Looking for your FM‑1";
  out.textContent = "";
  screen("idle", "Looking for your FM‑1");
  $("conn").dataset.state = "look";
  try {
    if (DEMO) { await sleep(1100); found = { text: "FM-1_015" }; }
    else {
      try { await midi(); } catch (_) { fail("nomidi"); return; }
      const dev = await updater.find(null, false) || await updater.find((id) => /^ota-/i.test(id.text), true);
      if (!dev) { fail("notfound", updater.seen()); return; }
      dev.link.close();
      found = dev.id;
    }
  } catch (e) { fail(e.code || "notfound", e.detail); return; }
  const ota = /^ota-/i.test(found.text);
  if (ota && !DEMO && !OUR_LOADER(found)) { fail("foreign", "(" + found.text + ")"); return; }
  $("connect-ask").hidden = true;
  $("conn").dataset.state = "ok";
  h.textContent = "Connected";
  out.textContent = ota ? "Your FM‑1 is waiting in update mode: Install will finish the update."
                        : "Your FM‑1 is ready. It is running " + nameOf(found.text) + ".";
  $("install-from").textContent = ota ? "An update is waiting to be finished." :
    found.text === PKG.product ? "POLY5 is already on it. Installing again puts version " + PKG.version + " on it and keeps your patches." :
    "POLY5 will replace " + nameOf(found.text) + ". You can go back at any time.";
  if (ota) screen("update", "Connected: in update mode", "UPDATE MODE", "WAITING FOR THE INSTALL");
  else if (found.text === PKG.product) screen("found", "Connected: POLY5");
  else { $("dev-other").textContent = found.text === "FM-1_015" ? "OFFICIAL V15" : found.text.replace("_", " "); screen("other", "Connected: " + nameOf(found.text)); }
  go.hidden = false;
}
$("connect-again").onclick = connect;
$("to-install").onclick = () => { open("install"); fetchPackage(); };

// ---- 4: install
function ready() { $("install-go").disabled = busy || !image || !$("agree").checked; }
$("agree").onchange = ready;
async function fetchPackage() {
  const st = $("pkg-state");
  if (image) return;
  st.textContent = "Getting POLY5…";
  try {
    const r = await fetch(PKG.file, { cache: "no-store" });
    if (!r.ok) throw new Error("http " + r.status);
    const raw = new Uint8Array(await r.arrayBuffer());
    if (await sha256hex(raw) !== PKG.sha256 || productOf(raw) !== PKG.product) throw new Error("checksum");
    image = logicalImage(raw);
    st.textContent = "POLY5 " + PKG.version + " with its 24 patches, " + Math.round(raw.length / 1024) + " KB. Checked: it is the file of the release.";
    ready();
  } catch (_) { st.textContent = ""; say($("install-err"), "package"); }
}
function progress(label, pct, warn, wait) {     // wait: nothing to count yet: a spinner, and the bar runs to and fro
  $("prog").hidden = false;
  $("prog-label").textContent = "";
  if (wait) { const s = document.createElement("span"); s.className = "spin"; $("prog-label").append(s); }
  $("prog-label").append(label);
  $("prog-bar").parentElement.classList.toggle("is-wait", !!wait);
  $("prog-bar").style.width = pct + "%";
  $("prog-bar").parentElement.setAttribute("aria-valuenow", String(Math.round(pct)));
  $("prog-warn").hidden = !warn;
}
function onStep(total) {
  return (what, n) => {
    if (what === "start") progress("Waking the updater on your FM‑1", 2, false, true);
    else if (what === "verify") progress("Your FM‑1 is checking the package", 4, false, true);
    else if (what === "loader") { progress("Switching to update mode", 6, true, true); screen("update", "Connected: installing", "UPDATE MODE", "WAITING FOR THE INSTALL"); }
    else if (what === "write") {
      const p = Math.min(99, Math.floor(n * 100 / total));
      progress("Writing " + p + "%", 6 + p * 0.9, true);
      screen("update", "Connected: installing", "WRITING", "DO NOT SWITCH OFF", p);
    }
    else if (what === "reboot") { progress("Restarting your FM‑1", 97, true, true); screen("update", "Connected: installing", "WRITING", "DO NOT SWITCH OFF", 100); }
    else if (what === "done") progress("Done", 100, false);
  };
}
async function install() {
  const err = $("install-err"), btn = $("install-go");
  if (busy || !image || !$("agree").checked) return;
  busy = true; btn.disabled = true; $("agree").disabled = true; err.hidden = true;
  $("agree-row").hidden = true; $("install-back").hidden = true; $("install-from").hidden = true;
  const total = Math.ceil(image.length / 512), step = onStep(total);
  $("install-h").textContent = "Installing";
  btn.hidden = true;
  progress("Preparing to start", 0, false, true);     // at once: the first answer from the FM-1 takes a few seconds
  try {
    if (DEMO) {
      step("start"); await sleep(700); step("verify"); await sleep(700); step("loader"); await sleep(1000);
      for (let n = 0; n <= total; n += Math.ceil(total / 50)) { step("write", n); await sleep(90); }
      step("reboot"); await sleep(1000); step("done");
    } else {
      // an FM-1 already in update mode (an install that was cut off): finish that write; else the whole install
      // (only looked for when the connection test found it in update mode: the search itself takes seconds)
      const resumed = /^ota-/i.test(found.text) && await updater.resume(image, step, { product: PKG.product });
      if (!resumed) await updater.install(image, PKG.product, step);
    }
    busy = false;
    screen("done", "Connected: POLY5 is on it");
    open("done");
  } catch (e) {
    busy = false; $("agree").disabled = false; ready();
    btn.hidden = false; btn.textContent = "Try again";
    $("prog-bar").parentElement.classList.remove("is-wait"); $("prog-label").textContent = "";
    $("install-h").textContent = "It stopped";
    $("prog-warn").hidden = true; $("install-back").hidden = false;
    say(err, e.code || "stopped", e.code ? "" : String(e.message || e));
  }
}
$("install-go").onclick = install;
addEventListener("beforeunload", (e) => { if (busy) { e.preventDefault(); e.returnValue = ""; } });

if (DEMO) $("demo-note").hidden = false;
checkBrowser();
open("browser");
