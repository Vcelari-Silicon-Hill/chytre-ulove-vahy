// Admin pages: login, password setup from an invitation, scales and people.
(function () {
  "use strict";
  const $ = (id) => document.getElementById(id);
  const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
  const fmtDate = new Intl.DateTimeFormat("cs-CZ", { day: "numeric", month: "numeric", year: "numeric", hour: "2-digit", minute: "2-digit" });
  const when = (ts) => (ts ? fmtDate.format(new Date(ts * 1000)) : "–");
  let me = null;

  async function api(method, url, body) {
    const r = await fetch(url, {
      method,
      credentials: "same-origin",
      headers: { "X-Requested-With": "fetch", ...(body ? { "Content-Type": "application/json" } : {}) },
      body: body ? JSON.stringify(body) : undefined,
    });
    let data = null;
    try { data = await r.json(); } catch {}
    if (!r.ok) {
      let msg = data && data.detail;
      if (Array.isArray(msg)) msg = msg.map((d) => (d.loc?.includes("password") || d.loc?.includes("new") ? "Heslo musí mít aspoň 10 znaků." : d.msg)).join(" ");
      const e = new Error(msg || `Chyba ${r.status}`);
      e.status = r.status;
      throw e;
    }
    return data;
  }

  function show(view) {
    document.querySelectorAll("section[data-view]").forEach((s) => { s.hidden = s.dataset.view !== view; });
  }
  function msg(el, text, kind = "err") {
    el.textContent = text;
    el.className = `msg ${kind}`;
    el.hidden = !text;
  }

  async function copy(text, btn) {
    try {
      await navigator.clipboard.writeText(text);
      btn.textContent = "Zkopírováno";
    } catch {
      btn.textContent = "Označte text a zkopírujte ručně";
    }
    setTimeout(() => { btn.textContent = btn.dataset.label; }, 2500);
  }
  function wireCopy(root) {
    root.querySelectorAll("button[data-copy]").forEach((b) => {
      b.dataset.label = b.textContent;
      b.addEventListener("click", () => copy(root.querySelector(b.dataset.copy).textContent, b));
    });
  }

  // ---------- start ----------
  async function start() {
    const m = location.hash.match(/^#heslo=([\w-]+)/);
    if (m) return startSetup(m[1]);
    try {
      me = await api("GET", "/api/v1/auth/me");
      startApp();
    } catch {
      show("login");
      $("login-email").focus();
    }
  }

  // ---------- login ----------
  $("login-form").addEventListener("submit", async (e) => {
    e.preventDefault();
    msg($("login-msg"), "");
    try {
      await api("POST", "/api/v1/auth/login", { email: $("login-email").value, password: $("login-pass").value });
      $("login-pass").value = "";
      me = await api("GET", "/api/v1/auth/me");
      startApp();
    } catch (err) {
      msg($("login-msg"), err.message);
    }
  });

  // ---------- set password from invitation ----------
  let setupToken = null;
  async function startSetup(token) {
    setupToken = token;
    history.replaceState(null, "", location.pathname);  // keep the token out of history
    show("setup");
    try {
      const info = await api("GET", `/api/v1/auth/invite?token=${encodeURIComponent(token)}`);
      $("setup-title").textContent = info.reset ? "Nové heslo" : "Vítejte ve správě vah";
      $("setup-email").textContent = `Účet: ${info.email}`;
      $("setup-user").value = info.email;
      $("setup-pass").focus();
    } catch (err) {
      msg($("setup-msg"), err.message);
      $("setup-form").querySelector("button").disabled = true;
    }
  }
  $("setup-form").addEventListener("submit", async (e) => {
    e.preventDefault();
    if ($("setup-pass").value !== $("setup-pass2").value) return msg($("setup-msg"), "Hesla se neshodují.");
    try {
      await api("POST", "/api/v1/auth/setup", { token: setupToken, password: $("setup-pass").value });
      me = await api("GET", "/api/v1/auth/me");
      startApp();
    } catch (err) {
      msg($("setup-msg"), err.message);
    }
  });

  // ---------- app ----------
  function startApp() {
    show("app");
    $("who").innerHTML = `<span>${esc(me.email)} · ${me.role === "admin" ? "správce" : "editor"}</span>
      <button class="btn" type="button" id="logout">Odhlásit</button><a href="/">veřejný přehled</a>`;
    $("logout").addEventListener("click", async () => { await api("POST", "/api/v1/auth/logout"); location.reload(); });
    $("pw-user").value = me.email;
    $("tab-people").hidden = me.role !== "admin";
    loadScales();
  }

  document.querySelector(".tabs").addEventListener("click", (e) => {
    const b = e.target.closest("button[data-tab]");
    if (!b) return;
    document.querySelectorAll(".tabs button").forEach((x) => x.setAttribute("aria-pressed", String(x === b)));
    document.querySelectorAll("[data-tab-panel]").forEach((p) => { p.hidden = p.dataset.tabPanel !== b.dataset.tab; });
    if (b.dataset.tab === "people") loadUsers();
    if (b.dataset.tab === "scales") loadScales();
  });

  // ---------- scales ----------
  function statusText(s, now) {
    if (!s.last_seen) return "čeká na první data";
    const ago = now - s.last_seen;
    const t = ago < 3600 ? `před ${Math.max(1, Math.round(ago / 60))} min` : ago < 172800 ? `před ${Math.round(ago / 3600)} h` : `před ${Math.round(ago / 86400)} dny`;
    return `${ago < 3 * 3600 ? "hlásí se" : "nehlásí se"} · ${t}`;
  }

  async function loadScales() {
    const data = await api("GET", "/api/v1/admin/scales");
    const ids = data.scales.map((s) => s.id);
    let n = 1;
    while (ids.includes(`vaha-${n}`)) n++;
    if (!$("ns-id").value) $("ns-id").placeholder = `vaha-${n}`;
    $("scales").innerHTML = data.scales.length ? data.scales.map((s) => `
      <tr data-id="${esc(s.id)}">
        <td><b>${esc(s.name)}</b><span class="sub2">${esc(s.id)}${s.location ? " · " + esc(s.location) : ""}</span></td>
        <td>${statusText(s, data.now)}${s.rssi ? `<span class="sub2">signál ${s.rssi} dBm${s.fails ? ` · ${s.fails}× nepovedené odeslání` : ""}</span>` : ""}</td>
        <td class="num">${s.readings.toLocaleString("cs-CZ")}</td>
        <td><span class="sub2">fw ${esc(s.fw || "–")}</span><span class="sub2 mono">${esc(s.mac || "")}</span></td>
        <td><input type="checkbox" data-act="public" ${s.public ? "checked" : ""} aria-label="Zobrazit na veřejném přehledu"></td>
        <td class="actions">
          <button class="btn" type="button" data-act="edit">Upravit</button>
          <button class="btn" type="button" data-act="token">Nový token</button>
          ${me.role === "admin" ? `<button class="btn danger" type="button" data-act="delete">Smazat</button>` : ""}
        </td>
      </tr>`).join("") : `<tr><td colspan="6" class="note">Zatím žádná váha. Přidejte první formulářem výše.</td></tr>`;
  }

  function showToken(id, token, isNew) {
    const box = $("token-box");
    const server = `${location.origin}/api/v1/ingest`;
    box.innerHTML = `<b>${isNew ? "Váha přidána." : "Nový token vydán, starý přestal platit."} Token se zobrazí jen teď.</b>
      <pre id="tok-text">ID váhy: ${esc(id)}\nToken:   ${esc(token)}\nServer:  ${esc(server)}</pre>
      <div class="row" style="display:flex;gap:8px;flex-wrap:wrap">
        <button class="btn" type="button" data-copy="#tok-text">Kopírovat</button>
        <a class="btn" href="/flash/">Otevřít nastavení váhy</a>
        <button class="btn" type="button" id="tok-close">Hotovo</button>
      </div>
      <p class="note" style="margin:0">Zadejte ID a token na stránce Nastavení úlové váhy (krok 3).${isNew ? "" : " Váha s předchozím tokenem přestane posílat data, dokud ho nezměníte i v ní."}</p>`;
    box.hidden = false;
    wireCopy(box);
    $("tok-close").addEventListener("click", () => { box.hidden = true; box.innerHTML = ""; });
    box.scrollIntoView({ behavior: "smooth", block: "nearest" });
  }

  $("add-scale").addEventListener("submit", async (e) => {
    e.preventDefault();
    msg($("ns-msg"), "");
    const id = $("ns-id").value.trim() || $("ns-id").placeholder;
    try {
      const r = await api("POST", "/api/v1/admin/scales", { id, name: $("ns-name").value.trim(), location: $("ns-loc").value.trim() });
      $("add-scale").reset();
      showToken(r.id, r.token, true);
      loadScales();
    } catch (err) {
      msg($("ns-msg"), err.message);
    }
  });

  $("scales").addEventListener("click", async (e) => {
    const b = e.target.closest("[data-act]");
    if (!b) return;
    const tr = b.closest("tr");
    const id = tr.dataset.id;
    const act = b.dataset.act;
    try {
      if (act === "public") {
        await api("PATCH", `/api/v1/admin/scales/${encodeURIComponent(id)}`, { public: b.checked });
      } else if (act === "edit") {
        const name = tr.querySelector("b").textContent;
        const sub = tr.querySelector(".sub2").textContent.split(" · ");
        tr.cells[0].innerHTML = `<div class="edit" style="display:grid;gap:4px">
          <input data-f="name" value="${esc(name)}" maxlength="80" aria-label="Název">
          <input data-f="location" value="${esc(sub.slice(1).join(" · "))}" maxlength="120" placeholder="místo" aria-label="Místo"></div>`;
        b.textContent = "Uložit";
        b.dataset.act = "save";
      } else if (act === "save") {
        await api("PATCH", `/api/v1/admin/scales/${encodeURIComponent(id)}`, {
          name: tr.querySelector('[data-f="name"]').value.trim(),
          location: tr.querySelector('[data-f="location"]').value.trim(),
        });
        loadScales();
      } else if (act === "token" || act === "delete") {
        confirmInline(b, act === "token" ? "Starý token přestane platit." : "Smaže váhu i všechna data.", async () => {
          if (act === "token") {
            const r = await api("POST", `/api/v1/admin/scales/${encodeURIComponent(id)}/token`);
            showToken(r.id, r.token, false);
          } else {
            await api("DELETE", `/api/v1/admin/scales/${encodeURIComponent(id)}`);
          }
          loadScales();
        });
      }
    } catch (err) {
      alertRow(tr, err.message);
    }
  });

  // in-page confirmation instead of confirm()
  function confirmInline(button, question, action) {
    const cell = button.parentElement;
    const original = cell.innerHTML;
    cell.innerHTML = `<span class="confirm">${esc(question)} <button class="btn danger" type="button" data-yes>Ano</button><button class="btn" type="button" data-no>Zpět</button></span>`;
    cell.querySelector("[data-no]").addEventListener("click", (e) => { e.stopPropagation(); cell.innerHTML = original; });
    cell.querySelector("[data-yes]").addEventListener("click", async (e) => {
      e.stopPropagation();
      try { await action(); } catch (err) { cell.innerHTML = original; alertRow(cell.closest("tr"), err.message); }
    });
  }
  function alertRow(tr, text) {
    const cell = tr.cells[tr.cells.length - 1];
    const m = document.createElement("div");
    m.className = "msg err";
    m.textContent = text;
    cell.appendChild(m);
    setTimeout(() => m.remove(), 5000);
  }

  // ---------- people ----------
  async function loadUsers() {
    const data = await api("GET", "/api/v1/admin/users");
    $("users").innerHTML = data.users.map((u) => `
      <tr data-id="${u.id}">
        <td><b>${esc(u.email)}</b>${u.invited_by ? `<span class="sub2">pozval(a) ${esc(u.invited_by)}</span>` : ""}</td>
        <td><select data-act="role" aria-label="Role" ${u.id === data.me ? "disabled" : ""}>
          <option value="editor" ${u.role === "editor" ? "selected" : ""}>editor</option>
          <option value="admin" ${u.role === "admin" ? "selected" : ""}>správce</option></select></td>
        <td>${u.active ? "aktivní" : "čeká na nastavení hesla"}${u.invite_expires ? `<span class="sub2">odkaz platí do ${when(u.invite_expires)}</span>` : ""}</td>
        <td>${when(u.last_login)}</td>
        <td class="actions">
          <button class="btn" type="button" data-act="link">${u.active ? "Odkaz na nové heslo" : "Nový odkaz"}</button>
          ${u.id === data.me ? "" : `<button class="btn danger" type="button" data-act="remove">Odebrat</button>`}
        </td>
      </tr>`).join("");
  }

  function showInvite(r) {
    const box = $("invite-box");
    const subject = r.reset ? "Nové heslo do správy včelích vah Silicon Hill" : "Přístup do správy včelích vah Silicon Hill";
    const body = r.reset
      ? `Ahoj,\n\nposílám odkaz, kterým si nastavíš nové heslo do správy včelích vah Silicon Hill. Platí ${r.days} dní a jde použít jen jednou:\n\n${r.link}\n\nPřihlašovací e-mail: ${r.email}\n`
      : `Ahoj,\n\nposílám ti přístup do správy včelích vah Silicon Hill. Heslo si nastavíš tady (odkaz platí ${r.days} dní a jde použít jen jednou):\n\n${r.link}\n\nPřihlašovací e-mail: ${r.email}\nPříště se přihlásíš na ${location.origin}/admin/\n`;
    const mailto = `mailto:${encodeURIComponent(r.email)}?subject=${encodeURIComponent(subject)}&body=${encodeURIComponent(body)}`;
    box.innerHTML = `<b>${r.reset ? "Odkaz na nové heslo" : "Pozvánka"} pro ${esc(r.email)} je připravená. Pošlete ji e-mailem.</b>
      <pre id="inv-subject">${esc(subject)}</pre>
      <pre id="inv-body">${esc(body)}</pre>
      <div style="display:flex;gap:8px;flex-wrap:wrap">
        <a class="btn primary" href="${mailto}">Otevřít v e-mailu</a>
        <button class="btn" type="button" data-copy="#inv-body">Kopírovat text</button>
        <button class="btn" type="button" data-copy="#inv-subject">Kopírovat předmět</button>
        <button class="btn" type="button" id="inv-close">Hotovo</button>
      </div>`;
    box.hidden = false;
    wireCopy(box);
    $("inv-close").addEventListener("click", () => { box.hidden = true; box.innerHTML = ""; });
    box.scrollIntoView({ behavior: "smooth", block: "nearest" });
  }

  $("invite-form").addEventListener("submit", async (e) => {
    e.preventDefault();
    msg($("inv-msg"), "");
    try {
      const r = await api("POST", "/api/v1/admin/users", { email: $("inv-email").value.trim(), role: $("inv-role").value });
      $("invite-form").reset();
      showInvite({ ...r, reset: false });
      loadUsers();
    } catch (err) {
      msg($("inv-msg"), err.message);
    }
  });

  $("users").addEventListener("change", async (e) => {
    if (e.target.dataset.act !== "role") return;
    const tr = e.target.closest("tr");
    try { await api("PATCH", `/api/v1/admin/users/${tr.dataset.id}`, { role: e.target.value }); }
    catch (err) { alertRow(tr, err.message); loadUsers(); }
  });
  $("users").addEventListener("click", async (e) => {
    const b = e.target.closest("button[data-act]");
    if (!b) return;
    const tr = b.closest("tr");
    try {
      if (b.dataset.act === "link") {
        showInvite(await api("POST", `/api/v1/admin/users/${tr.dataset.id}/link`));
        loadUsers();
      } else if (b.dataset.act === "remove") {
        confirmInline(b, "Odebrat přístup?", async () => { await api("DELETE", `/api/v1/admin/users/${tr.dataset.id}`); loadUsers(); });
      }
    } catch (err) {
      alertRow(tr, err.message);
    }
  });

  // ---------- account ----------
  $("pw-form").addEventListener("submit", async (e) => {
    e.preventDefault();
    try {
      await api("POST", "/api/v1/auth/password", { old: $("pw-old").value, new: $("pw-new").value });
      $("pw-form").reset();
      $("pw-user").value = me.email;
      msg($("pw-msg"), "Heslo změněno.", "ok");
    } catch (err) {
      msg($("pw-msg"), err.message);
    }
  });

  start();
})();
