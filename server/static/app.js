// Public dashboard: overview cards + detail charts (uPlot).
(function () {
  "use strict";
  const fmt1 = new Intl.NumberFormat("cs-CZ", { minimumFractionDigits: 1, maximumFractionDigits: 1 });
  const fmt2 = new Intl.NumberFormat("cs-CZ", { minimumFractionDigits: 2, maximumFractionDigits: 2 });
  const fmtTime = new Intl.DateTimeFormat("cs-CZ", { day: "numeric", month: "numeric", hour: "2-digit", minute: "2-digit" });
  const $ = (id) => document.getElementById(id);
  const css = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();
  const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

  let state = { scales: [], selected: null, days: 7, series: null, charts: [] };

  function kg(v, digits = 1) {
    if (v == null) return "–";
    return (digits === 2 ? fmt2 : fmt1).format(v);
  }
  function signed(v) {
    if (v == null) return '<span class="num">–</span>';
    const cls = v > 0.05 ? "up" : v < -0.05 ? "down" : "";
    const sign = v > 0 ? "+" : v < 0 ? "−" : "";
    return `<span class="num ${cls}">${sign}${fmt1.format(Math.abs(v))} kg</span>`;
  }
  function ago(ts, now) {
    if (!ts) return "zatím nic";
    const s = now - ts;
    if (s < 90) return "právě teď";
    if (s < 3600) return `před ${Math.round(s / 60)} min`;
    if (s < 86400 * 2) return `před ${Math.round(s / 3600)} h`;
    return `před ${Math.round(s / 86400)} dny`;
  }
  const STATUS = { ok: "hlásí se", nehlasi: "nehlásí se", ceka: "čeká na data" };

  function sparkPath(spark) {
    const pts = spark.ts.map((t, i) => [t, spark.net[i]]).filter((p) => p[1] != null);
    if (pts.length < 2) return "";
    const t0 = pts[0][0], t1 = pts[pts.length - 1][0];
    let lo = Math.min(...pts.map((p) => p[1])), hi = Math.max(...pts.map((p) => p[1]));
    if (hi - lo < 1) { const m = (hi + lo) / 2; lo = m - 0.5; hi = m + 0.5; }
    const W = 240, H = 40;
    const d = pts.map((p, i) => `${i ? "L" : "M"}${(((p[0] - t0) / (t1 - t0 || 1)) * W).toFixed(1)},${(H - 2 - ((p[1] - lo) / (hi - lo)) * (H - 4)).toFixed(1)}`).join("");
    return `<svg class="spark" viewBox="0 0 ${W} ${H}" preserveAspectRatio="none" aria-hidden="true"><path d="${d}" fill="none" stroke="var(--chart-net)" stroke-width="1.8" vector-effect="non-scaling-stroke"/></svg>`;
  }

  function renderCards(now) {
    const el = $("cards");
    if (!state.scales.length) {
      el.innerHTML = `<div class="empty">Zatím tu není žádná váha. Váhu přidá správce serveru, pak ji nastavíte na stránce <a href="/flash/">Nahrát firmware do váhy</a>.</div>`;
      return;
    }
    el.innerHTML = state.scales.map((s) => {
      const temp = s.t_cell != null ? `${fmt1.format(s.t_cell)} °C` : "–";
      const bat = s.battery_pct != null ? `${s.battery_pct} % · ${fmt2.format(s.vbat)} V` : "–";
      return `<button type="button" class="card" data-id="${esc(s.id)}" aria-current="${s.id === state.selected}">
        <div class="head"><span class="name">${esc(s.name)}</span><span class="chip ${s.status}">${STATUS[s.status] || s.status}</span></div>
        ${s.location ? `<div class="loc">${esc(s.location)}</div>` : ""}
        <div class="big">${kg(s.kg)} <small>kg</small></div>
        <dl class="rows">
          <dt>za 24 hodin</dt><dd>${signed(s.change_24h)}</dd>
          <dt>za týden</dt><dd>${signed(s.change_7d)}</dd>
          <dt>teplota pod úlem</dt><dd>${temp}</dd>
          <dt>baterie</dt><dd>${bat}</dd>
          <dt>poslední data</dt><dd>${ago(s.last_seen, now)}</dd>
        </dl>
        ${sparkPath(s.spark)}
      </button>`;
    }).join("");
    el.querySelectorAll(".card").forEach((b) => b.addEventListener("click", () => select(b.dataset.id, true)));
  }

  async function loadOverview() {
    const r = await fetch("/api/v1/scales", { cache: "no-store" });
    const data = await r.json();
    state.scales = data.scales;
    document.title = data.title;
    $("title").textContent = data.title;
    if (data.repo) $("repo").href = data.repo;
    $("meta").innerHTML = `aktualizováno ${fmtTime.format(new Date(data.now * 1000))}<br>${data.scales.length} ${data.scales.length === 1 ? "váha" : data.scales.length < 5 ? "váhy" : "vah"}`;
    const fromHash = decodeURIComponent(location.hash.slice(1));
    if (!state.selected) state.selected = state.scales.some((s) => s.id === fromHash) ? fromHash : state.scales[0]?.id || null;
    renderCards(data.now);
  }

  function select(id, scroll) {
    state.selected = id;
    if (location.hash.slice(1) !== id) history.replaceState(null, "", "#" + encodeURIComponent(id));
    document.querySelectorAll(".card").forEach((c) => c.setAttribute("aria-current", String(c.dataset.id === id)));
    loadSeries().then(() => { if (scroll) $("detail").scrollIntoView({ behavior: "smooth", block: "start" }); });
  }

  async function loadSeries() {
    if (!state.selected) { $("detail").hidden = true; return; }
    const r = await fetch(`/api/v1/scales/${encodeURIComponent(state.selected)}/series?days=${state.days}`, { cache: "no-store" });
    if (!r.ok) return;
    state.series = await r.json();
    $("detail").hidden = false;
    $("detail-name").textContent = state.series.name;
    $("csv").href = `/api/v1/scales/${encodeURIComponent(state.selected)}/csv?days=${Math.max(state.days, 30)}`;
    drawCharts();
    renderEvents();
  }

  const KIND = { "manipulace+": "přidáno (nástavek, krmení…)", "manipulace-": "odebráno (medobraní, kontrola…)", "roj?": "možný roj" };
  function renderEvents() {
    const ev = state.series.events.slice().reverse();
    $("events").innerHTML = ev.length
      ? ev.map((e) => `<li><time>${fmtTime.format(new Date(e.ts * 1000))}</time><span>${KIND[e.kind] || e.kind}</span><span class="num">${e.delta > 0 ? "+" : "−"}${fmt1.format(Math.abs(e.delta))} kg</span></li>`).join("")
      : `<li class="note">Žádné skoky ani možné roje.</li>`;
  }

  const fmtHM = new Intl.DateTimeFormat("cs-CZ", { hour: "2-digit", minute: "2-digit" });
  const fmtDM = new Intl.DateTimeFormat("cs-CZ", { day: "numeric", month: "numeric" });
  function timeTicks(u, splits) {
    return splits.map((ts) => {
      const d = new Date(ts * 1000);
      return d.getHours() === 0 && d.getMinutes() === 0 ? fmtDM.format(d) : fmtHM.format(d);
    });
  }

  function axisOpts(unit, digits = 1) {
    const nf = digits === 2 ? fmt2 : fmt1;
    const grid = { stroke: css("--line"), width: 1 };
    const ticks = { stroke: css("--line"), width: 1 };
    return [
      { stroke: css("--muted"), grid, ticks, font: `12px ${css("--font-body")}`, values: timeTicks },
      { stroke: css("--muted"), grid, ticks, font: `12px ${css("--font-body")}`, size: 56, values: (u, v) => v.map((x) => x == null ? "" : `${nf.format(x)} ${unit}`) },
    ];
  }

  function eventMarkers(events) {
    return {
      hooks: {
        draw: [(u) => {
          const ctx = u.ctx;
          ctx.save();
          ctx.strokeStyle = css("--accent");
          ctx.setLineDash([4, 4]);
          ctx.lineWidth = 1 * devicePixelRatio;
          for (const e of events) {
            const x = Math.round(u.valToPos(e.ts, "x", true));
            if (x < u.bbox.left || x > u.bbox.left + u.bbox.width) continue;
            ctx.beginPath();
            ctx.moveTo(x, u.bbox.top);
            ctx.lineTo(x, u.bbox.top + u.bbox.height);
            ctx.stroke();
          }
          ctx.restore();
        }],
      },
    };
  }

  function makeChart(target, height, unit, series, data, plugins, digits) {
    const width = target.clientWidth || 600;
    const opts = {
      width, height,
      cursor: { drag: { x: true, y: false } },
      scales: { x: { time: true } },
      axes: axisOpts(unit, digits),
      series: [{}, ...series],
      plugins: plugins || [],
      legend: { show: false },
    };
    target.innerHTML = "";
    return new uPlot(opts, data, target);
  }

  function drawCharts() {
    state.charts.forEach((c) => c.destroy());
    state.charts = [];
    const s = state.series;
    const ts = s.ts;
    const line = (color, width = 2, dash) => ({ stroke: css(color), width, dash, points: { show: false }, spanGaps: false });
    state.charts.push(makeChart($("ch-weight"), 280, "kg",
      [{ label: "čistá", ...line("--chart-net", 2.2) }, { label: "naměřená", ...line("--chart-raw", 1.2, [4, 3]) }],
      [ts, s.net, s.kg], [eventMarkers(s.events)]));
    state.charts.push(makeChart($("ch-temp"), 200, "°C",
      [{ label: "pod úlem", ...line("--chart-cell") }, { label: "krabice", ...line("--chart-box", 1.6) }],
      [ts, s.t_cell, s.t_box]));
    state.charts.push(makeChart($("ch-bat"), 140, "V",
      [{ label: "baterie", ...line("--chart-bat", 1.8) }],
      [ts, s.vbat], [], 2));
  }

  $("ranges").addEventListener("click", (e) => {
    const b = e.target.closest("button[data-days]");
    if (!b) return;
    state.days = Number(b.dataset.days);
    $("ranges").querySelectorAll("button").forEach((x) => x.setAttribute("aria-pressed", String(x === b)));
    loadSeries();
  });
  let resizeT;
  window.addEventListener("resize", () => { clearTimeout(resizeT); resizeT = setTimeout(() => state.series && drawCharts(), 150); });
  matchMedia("(prefers-color-scheme: dark)").addEventListener("change", () => state.series && drawCharts());
  window.addEventListener("hashchange", () => {
    const id = decodeURIComponent(location.hash.slice(1));
    if (id && id !== state.selected && state.scales.some((s) => s.id === id)) select(id, false);
  });

  async function refresh() {
    try {
      await loadOverview();
      await loadSeries();
    } catch (e) {
      $("meta").textContent = "Server teď neodpovídá, zkusím to znovu.";
    }
  }
  refresh();
  setInterval(refresh, 5 * 60 * 1000);
})();
