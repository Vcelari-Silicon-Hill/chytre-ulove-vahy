// Scale setup over Web Serial: talks to the firmware console (one command per line, "@" lines carry JSON).
(function () {
  "use strict";
  const $ = (id) => document.getElementById(id);
  const fmt1 = new Intl.NumberFormat("cs-CZ", { minimumFractionDigits: 1, maximumFractionDigits: 1 });
  const fmt2 = new Intl.NumberFormat("cs-CZ", { minimumFractionDigits: 2, maximumFractionDigits: 2 });

  const ACTION_IDS = ["save", "wifitest", "scan", "upload", "livebtn", "tare", "cal", "dsfind", "run", "cmd", "send", "disconnect"];
  let port = null, reader = null, writer = null, readDone = null;
  let buffer = "";
  let waiters = [];
  let queue = Promise.resolve();
  let liveTimer = null, livePending = false;

  if (!("serial" in navigator)) {
    $("nosupport").hidden = false;
    $("connect").disabled = true;
  }
  fetch("manifest.json").then((r) => r.json()).then((m) => { $("fwver").textContent = `verze firmwaru ${m.version}`; }).catch(() => {});
  if (!$("url").value) $("url").value = `${location.origin}/api/v1/ingest`;
  // ID and token handed over from the admin page; the fragment never reaches the server
  const handoff = new URLSearchParams(location.hash.slice(1));
  if (handoff.get("token")) {
    $("scale_id").value = handoff.get("id") || "";
    $("token").value = handoff.get("token");
    history.replaceState(null, "", location.pathname);  // keep the token out of history
  }
  // Wi-Fi settings remembered in this browser only, so the next scale needs just ID and token
  const WIFI_KEYS = ["wifitype", "ssid", "pass", "eap_user", "eap_pass", "eap_ca"];
  try {
    const saved = JSON.parse(localStorage.getItem("vahy-wifi") || "null");
    if (saved) WIFI_KEYS.forEach((k) => { if (saved[k]) $(k).value = saved[k]; });
  } catch {}

  // ---------- log + line handling ----------
  function log(text) {
    const el = $("log");
    el.textContent += text + "\n";
    if (el.textContent.length > 60000) el.textContent = el.textContent.slice(-40000);
    el.scrollTop = el.scrollHeight;
  }

  function parseAt(line, tag) {
    try { return JSON.parse(line.slice(tag.length + 2)); } catch { return null; }
  }

  function onLine(line) {
    log(line);
    if (line.startsWith("@hello ")) {
      const h = parseAt(line, "hello");
      if (h) $("devinfo").textContent = `firmware ${h.fw} · MAC ${h.mac}`;
    } else if (line.startsWith("@config ")) {
      const c = parseAt(line, "config");
      if (c) fillForm(c);
    } else if (line.startsWith("@reading ")) {
      const r = parseAt(line, "reading");
      if (r) showLive(r);
    } else if (line.startsWith("@mac ")) {
      $("mac").textContent = `MAC adresa pro registraci v síti: ${line.slice(5).replace(/"/g, "")}`;
    }
    waiters = waiters.filter((w) => {
      if (w.match.test(line)) { w.resolve(line); return false; }
      return true;
    });
  }

  async function readLoop() {
    const decoder = new TextDecoderStream();
    readDone = port.readable.pipeTo(decoder.writable).catch(() => {});
    reader = decoder.readable.getReader();
    try {
      for (;;) {
        const { value, done } = await reader.read();
        if (done) break;
        buffer += value;
        let i;
        while ((i = buffer.indexOf("\n")) >= 0) {
          const line = buffer.slice(0, i).replace(/\r$/, "");
          buffer = buffer.slice(i + 1);
          if (line.length) onLine(line);
        }
      }
    } catch (e) {
      log(`[spojení přerušeno: ${e.message}]`);
    } finally {
      cleanup();
    }
  }

  function write(line) {
    log(`> ${line.startsWith("set token") || line.startsWith("set pass") || line.startsWith("set eap_pass") ? line.split(" ").slice(0, 2).join(" ") + " ***" : line}`);
    return writer.write(new TextEncoder().encode(line + "\n"));
  }

  // send a command and wait for a matching reply line
  function cmd(line, match, timeoutMs = 8000) {
    const job = queue.then(() => new Promise((resolve, reject) => {
      if (!writer) return reject(new Error("nepřipojeno"));
      const w = { match, resolve };
      waiters.push(w);
      const t = setTimeout(() => {
        waiters = waiters.filter((x) => x !== w);
        reject(new Error(`váha neodpověděla na „${line.split(" ")[0]}“`));
      }, timeoutMs);
      w.resolve = (l) => { clearTimeout(t); resolve(l); };
      write(line).catch(reject);
    }));
    queue = job.catch(() => {});
    return job;
  }
  const OKERR = /^(OK|ERR)\b/;

  // ---------- connection ----------
  function setConnected(on) {
    $("status").textContent = on ? "připojeno" : "nepřipojeno";
    $("status").className = "status " + (on ? "on" : "off");
    $("connect").disabled = on || !("serial" in navigator);
    ACTION_IDS.forEach((id) => { $(id).disabled = !on; });
  }

  function cleanup() {
    stopLive();
    writer = null;
    setConnected(false);
  }

  $("connect").addEventListener("click", async () => {
    try {
      port = await navigator.serial.requestPort();
      await port.open({ baudRate: 115200 });
    } catch (e) {
      if (e.name !== "NotFoundError") log(`[nelze otevřít port: ${e.message}]`);
      return;
    }
    writer = port.writable.getWriter();
    setConnected(true);
    readLoop();
    try {
      await cmd("show", /^@config /, 3000);
      await cmd("mac", /^@mac /, 3000);
    } catch {
      log("[Váha neodpovídá. Stiskněte RESET, počkejte 2 s a připojte se znovu do 20 sekund.]");
    }
  });

  $("disconnect").addEventListener("click", disconnect);
  async function disconnect() {
    stopLive();
    try { if (reader) await reader.cancel(); } catch {}
    try { if (readDone) await readDone; } catch {}
    try { if (writer) writer.releaseLock(); } catch {}
    try { if (port) await port.close(); } catch {}
    port = null;
    cleanup();
  }

  // ---------- form ----------
  function fillForm(c) {
    for (const k of ["url", "ssid", "eap_user"]) if (c[k]) $(k).value = c[k];
    if (c.ssid) $("wifitype").value = c.eap_user ? "eap" : "psk";
    if (c.scale_id && !$("token").value) $("scale_id").value = c.scale_id;  // a new token comes with its own ID
    $("tls").value = c.tls || "verify";
    if (c.interval) $("interval").value = c.interval;
    $("token").placeholder = c.token ? "uloženo – vyplňte jen pro změnu" : "prázdné = vytvořit nový";
    $("pass").placeholder = c.pass ? "uloženo – vyplňte jen pro změnu" : "";
    $("eap_pass").placeholder = c.eap_pass ? "uloženo – vyplňte jen pro změnu" : "";
    if (!c.url) $("url").value = `${location.origin}/api/v1/ingest`;
    toggleWifiType();
  }

  function toggleWifiType() {
    const eap = $("wifitype").value === "eap";
    document.querySelectorAll("[data-eap]").forEach((el) => { el.hidden = !eap; });
    document.querySelectorAll("[data-psk]").forEach((el) => { el.hidden = eap; });
  }
  $("wifitype").addEventListener("change", toggleWifiType);
  toggleWifiType();

  async function run(button, fn) {
    button.disabled = true;
    try { await fn(); } catch (e) { log(`[chyba: ${e.message}]`); } finally { if (writer) button.disabled = false; }
  }

  // New scale: register it on the server with a random ID (rename later in admin); needs the admin login cookie
  async function registerScale() {
    const id = `vaha-${crypto.getRandomValues(new Uint32Array(1))[0].toString(36).slice(0, 5)}`;
    const r = await fetch("/api/v1/admin/scales", {
      method: "POST",
      credentials: "same-origin",
      headers: { "X-Requested-With": "fetch", "Content-Type": "application/json" },
      body: JSON.stringify({ id, name: id }),
    });
    if (r.status === 401) throw new Error("pro automatické ID a token se nejdřív přihlaste ve správě (/admin/) a zkuste to znovu");
    const data = await r.json().catch(() => ({}));
    if (!r.ok) throw new Error(data.detail || `server vrátil ${r.status}`);
    $("scale_id").value = data.id;
    $("token").value = data.token;
    log(`[Na serveru vytvořena nová váha ${data.id}, název změníte ve správě.]`);
  }

  $("save").addEventListener("click", () => run($("save"), async () => {
    const sets = [];
    const add = (k, v) => sets.push(`set ${k} ${v}`);
    if (!$("scale_id").value.trim()) await registerScale();  // a token without an ID is useless (usually browser autofill)
    const id = $("scale_id").value.trim();
    if (!/^[a-z0-9][a-z0-9-]{0,31}$/.test(id)) throw new Error(`ID váhy „${id}“ není platné (jen malá písmena, číslice a pomlčky). Smažte ho a nové se vytvoří samo.`);
    add("scale_id", id);
    add("url", $("url").value.trim());
    add("ssid", $("ssid").value.trim());
    add("tls", $("tls").value);
    add("interval", String(Math.max(60, parseInt($("interval").value, 10) || 900)));
    if ($("token").value.trim()) add("token", $("token").value.trim());
    if ($("wifitype").value === "psk") {
      add("eap_user", "-");
      if ($("pass").value) add("pass", $("pass").value);
    } else {
      add("eap_user", $("eap_user").value.trim());
      if ($("eap_pass").value) add("eap_pass", $("eap_pass").value);
      if ($("eap_ca").value.trim()) add("eap_ca", $("eap_ca").value.trim().replace(/\r?\n/g, "\\n"));
    }
    for (const s of sets) {
      const r = await cmd(s, OKERR);
      if (r.startsWith("ERR")) throw new Error(r);
    }
    try {
      if ($("remember").checked) localStorage.setItem("vahy-wifi", JSON.stringify(Object.fromEntries(WIFI_KEYS.map((k) => [k, $(k).value]))));
      else localStorage.removeItem("vahy-wifi");
    } catch {}
    $("token").value = "";
    await cmd("show", /^@config /);
    log("[Nastavení uloženo do váhy.]");
  }));

  $("wifitest").addEventListener("click", () => run($("wifitest"), async () => {
    const line = await cmd("wifi", /^(@wifi |ERR)/, 40000);
    if (line.startsWith("ERR")) throw new Error(`${line} – nejdřív klikněte na Uložit do váhy`);
    const r = JSON.parse(line.replace(/^@wifi /, "") || "{}");
    if (r.ok) { log(`[Wi-Fi OK: signál ${r.rssi} dBm, připojení za ${r.ms} ms, IP ${r.ip}]`); return; }
    // ESP-IDF wifi_err_reason_t codes, firmware 1.0.1+
    const why = { 201: "síť nenalezena: nevysílá na 2,4 GHz, je daleko, nebo se název liší (velká písmena, mezery)",
      15: "špatné heslo", 202: "špatné heslo nebo síť odmítla přihlášení", 204: "špatné heslo", 2: "špatné heslo",
      210: "síť používá zabezpečení, které váha neumí (zkuste WPA2)" }[r.reason];
    log(`[Wi-Fi se nepřipojila: ${why || (r.why ? `kód ${r.reason} ${r.why}` : "zkontrolujte název sítě, heslo a registraci MAC adresy")}.]`);
    if (r.reason !== undefined) {
      log("[Sítě, které váha vidí (jen 2,4 GHz):]");
      await cmd("scan", /^@scan /, 20000);
    }
  }));

  $("scan").addEventListener("click", () => run($("scan"), async () => {
    log("[Sítě, které váha vidí (jen 2,4 GHz):]");
    await cmd("scan", /^(@scan |ERR)/, 20000);
  }));

  $("showpw").addEventListener("change", (e) => {
    ["token", "pass", "eap_pass"].forEach((k) => { $(k).type = e.target.checked ? "text" : "password"; });
  });

  $("upload").addEventListener("click", () => run($("upload"), async () => {
    const r = await cmd("upload", /^(OK server|ERR)/, 60000);
    log(r.startsWith("OK") ? "[Data dorazila na server. Váha je vidět na přehledu.]" : "[Odeslání selhalo, viz řádek výše. 401/403 = špatné ID nebo token.]");
  }));

  // ---------- calibration ----------
  function showLive(r) {
    $("lv-kg").textContent = r.kg == null ? "nekalibrováno" : `${fmt2.format(r.kg)} kg`;
    $("lv-raw").textContent = r.scale_ok ? String(r.raw) : "převodník nenalezen";
    $("lv-tc").textContent = r.t_cell == null ? "–" : `${fmt1.format(r.t_cell)} °C`;
    $("lv-tb").textContent = r.t_box == null ? "–" : `${fmt1.format(r.t_box)} °C`;
    $("lv-vb").textContent = r.vbat == null ? "–" : `${fmt2.format(r.vbat)} V`;
  }

  function stopLive() {
    clearInterval(liveTimer);
    liveTimer = null;
    $("livebtn").textContent = "Živé měření";
    $("livebtn").setAttribute("aria-pressed", "false");
  }
  $("livebtn").addEventListener("click", () => {
    if (liveTimer) return stopLive();
    $("livebtn").textContent = "Zastavit měření";
    $("livebtn").setAttribute("aria-pressed", "true");
    const tick = async () => {
      if (livePending || !writer) return;
      livePending = true;
      try { await cmd("read", /^@reading /, 10000); } catch {} finally { livePending = false; }
    };
    tick();
    liveTimer = setInterval(tick, 4000);
  });

  $("tare").addEventListener("click", () => run($("tare"), async () => {
    const r = await cmd("tare", OKERR, 20000);
    log(r.startsWith("OK") ? "[Tára uložena.]" : "[Tára se nepovedla.]");
  }));

  $("cal").addEventListener("click", () => run($("cal"), async () => {
    const kgv = parseFloat(String($("calkg").value).replace(",", "."));
    if (!(kgv > 0)) throw new Error("zadejte hmotnost závaží v kg");
    const r = await cmd(`cal ${kgv}`, OKERR, 30000);
    log(r.startsWith("OK") ? "[Kalibrace uložena. Zkontrolujte živé měření: sundejte a znovu položte závaží.]" : "[Kalibrace se nepovedla.]");
  }));

  $("dsfind").addEventListener("click", () => run($("dsfind"), async () => {
    log("[Zahřejte v dlani teploměr přilepený na tenzometru. Čekám až 60 s…]");
    await cmd("dsfind", OKERR, 75000);
  }));

  $("run").addEventListener("click", () => run($("run"), async () => {
    await cmd("run", /^OK spoustim/, 5000);
    log("[Váha začala měřit. Po odeslání dat usne, port se odpojí.]");
  }));

  // ---------- manual console ----------
  function sendManual() {
    const v = $("cmd").value.trim();
    if (!v || !writer) return;
    write(v);
    $("cmd").value = "";
  }
  $("send").addEventListener("click", sendManual);
  $("cmd").addEventListener("keydown", (e) => { if (e.key === "Enter") sendManual(); });

  if ("serial" in navigator) navigator.serial.addEventListener("disconnect", (e) => { if (e.target === port) { log("[USB odpojeno]"); cleanup(); } });
  setConnected(false);
})();
