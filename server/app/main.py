"""Chytré úly pro Silicon Hill – data server and public dashboard.

POST /api/v1/ingest            scales upload measurements here (Bearer token per scale)
GET  /api/v1/scales            public overview of all scales
GET  /api/v1/scales/{id}/series?days=7
GET  /api/v1/scales/{id}/csv?days=30
GET  /                         public dashboard
GET  /flash/                   firmware installer and scale setup in the browser
GET  /admin/                   management: scales and people (login required), see auth.py
"""
import csv
import io
import os
import time
from typing import Optional

from contextlib import asynccontextmanager

from fastapi import FastAPI, Header, HTTPException, Query
from fastapi.responses import PlainTextResponse, RedirectResponse, StreamingResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

from . import auth, db
from .processing import battery_percent, change_over, downsample, net_weight

STATIC = os.path.join(os.path.dirname(__file__), "..", "static")
SITE_TITLE = os.environ.get("SITE_TITLE", "Včelí váhy Silicon Hill")
REPO_URL = os.environ.get("REPO_URL", "https://github.com/Vcelari-Silicon-Hill/chytre-ulove-vahy")
STALE_AFTER_S = int(os.environ.get("STALE_AFTER_S", 3 * 3600))
MAX_POINTS = 1500


@asynccontextmanager
async def lifespan(_app):
    db.init()
    link = auth.ensure_first_admin()
    if link:
        print(f"\n*** Odkaz pro nastavení hesla správce {os.environ.get('ADMIN_EMAIL')} (platí {auth.INVITE_DAYS} dní):\n*** {link}\n"
              "*** Bez PUBLIC_URL je to jen cesta, doplňte před ni adresu serveru.\n", flush=True)
    yield


app = FastAPI(title=SITE_TITLE, docs_url="/api/docs", redoc_url=None, lifespan=lifespan)
app.include_router(auth.router)


# ---------- ingest ----------

class Reading(BaseModel):
    age: int = Field(ge=0, le=60 * 86400)  # seconds before upload
    raw: Optional[int] = None
    kg: Optional[float] = None
    tc: Optional[float] = None             # load cell temperature
    tb: Optional[float] = None             # box temperature
    vb: Optional[float] = None             # battery voltage


class Upload(BaseModel):
    scale: str
    fw: Optional[str] = None
    mac: Optional[str] = None
    rssi: Optional[int] = None
    boot: Optional[int] = None
    fails: Optional[int] = None
    readings: list[Reading] = Field(max_length=500)


def _plausible(v: Optional[float], lo: float, hi: float) -> Optional[float]:
    return v if v is not None and lo <= v <= hi else None


@app.post("/api/v1/ingest")
def ingest(body: Upload, authorization: str = Header(default="")):
    token = authorization.removeprefix("Bearer ").strip()
    if not token:
        raise HTTPException(401, "missing token")
    now = int(time.time())
    with db.db() as con:
        row = con.execute("SELECT id FROM scales WHERE token_hash=?", (db.hash_token(token),)).fetchone()
        if row is None or row["id"] != body.scale:
            raise HTTPException(403, "unknown scale or wrong token")
        rows = [
            (body.scale, now - r.age, r.raw, _plausible(r.kg, -50, 400), _plausible(r.tc, -40, 85),
             _plausible(r.tb, -40, 85), _plausible(r.vb, 0, 6))
            for r in body.readings
        ]
        cur = con.executemany(
            "INSERT OR IGNORE INTO readings (scale_id, ts, raw, kg, t_cell, t_box, vbat) VALUES (?,?,?,?,?,?,?)", rows
        )
        con.execute(
            "UPDATE scales SET last_seen=?, fw=COALESCE(?,fw), mac=COALESCE(?,mac), rssi=COALESCE(?,rssi), fails=? WHERE id=?",
            (now, body.fw, body.mac, body.rssi, body.fails, body.scale),
        )
    return {"ok": True, "stored": cur.rowcount, "server_time": now}


# ---------- public API ----------

def _load(con, scale_id: str, since: int) -> dict[str, list]:
    rows = con.execute(
        "SELECT ts, kg, t_cell, t_box, vbat FROM readings WHERE scale_id=? AND ts>=? ORDER BY ts", (scale_id, since)
    ).fetchall()
    return {
        "ts": [r["ts"] for r in rows],
        "kg": [r["kg"] for r in rows],
        "t_cell": [r["t_cell"] for r in rows],
        "t_box": [r["t_box"] for r in rows],
        "vbat": [r["vbat"] for r in rows],
    }


def _status(last_seen: Optional[int], now: int) -> str:
    if last_seen is None:
        return "ceka"          # waiting for the first data
    return "ok" if now - last_seen < STALE_AFTER_S else "nehlasi"


@app.get("/api/v1/scales")
def scales():
    now = int(time.time())
    out = []
    with db.db() as con:
        for s in con.execute("SELECT * FROM scales WHERE public=1 ORDER BY id").fetchall():
            data = _load(con, s["id"], now - 8 * 86400)
            net, events = net_weight(data["ts"], data["kg"])
            last = {}
            for key in ("kg", "t_cell", "t_box", "vbat"):
                vals = [(t, v) for t, v in zip(data["ts"], data[key]) if v is not None]
                last[key] = vals[-1][1] if vals else None
            spark = downsample({"ts": data["ts"], "net": net}, 120)
            out.append({
                "id": s["id"], "name": s["name"], "location": s["location"],
                "status": _status(s["last_seen"], now), "last_seen": s["last_seen"],
                "fw": s["fw"], "rssi": s["rssi"],
                "kg": last["kg"], "t_cell": last["t_cell"], "t_box": last["t_box"], "vbat": last["vbat"],
                "battery_pct": battery_percent(last["vbat"]),
                "change_24h": change_over(data["ts"], net, 86400),
                "change_7d": change_over(data["ts"], net, 7 * 86400),
                "spark": spark,
                "events": [e for e in events if e["ts"] >= now - 86400 * 2],
            })
    return {"title": SITE_TITLE, "repo": REPO_URL, "now": now, "scales": out}


def _scale_or_404(con, scale_id: str):
    s = con.execute("SELECT * FROM scales WHERE id=? AND public=1", (scale_id,)).fetchone()
    if s is None:
        raise HTTPException(404, "scale not found")
    return s


@app.get("/api/v1/scales/{scale_id}/series")
def series(scale_id: str, days: float = Query(7, gt=0, le=800)):
    now = int(time.time())
    with db.db() as con:
        s = _scale_or_404(con, scale_id)
        data = _load(con, scale_id, int(now - days * 86400))
    net, events = net_weight(data["ts"], data["kg"])
    data["net"] = net
    return {"id": s["id"], "name": s["name"], "days": days, "events": events, **downsample(data, MAX_POINTS)}


@app.get("/api/v1/scales/{scale_id}/csv")
def export_csv(scale_id: str, days: float = Query(30, gt=0, le=3650)):
    now = int(time.time())
    with db.db() as con:
        _scale_or_404(con, scale_id)
        data = _load(con, scale_id, int(now - days * 86400))
    net, _ = net_weight(data["ts"], data["kg"])
    buf = io.StringIO()
    w = csv.writer(buf)
    w.writerow(["time_utc", "unix", "weight_kg", "net_weight_kg", "t_load_cell_c", "t_box_c", "battery_v"])
    for i, t in enumerate(data["ts"]):
        w.writerow([time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(t)), t, data["kg"][i], net[i],
                    data["t_cell"][i], data["t_box"][i], data["vbat"][i]])
    buf.seek(0)
    return StreamingResponse(iter([buf.getvalue()]), media_type="text/csv",
                             headers={"Content-Disposition": f'attachment; filename="{scale_id}.csv"'})


@app.get("/healthz", response_class=PlainTextResponse)
def healthz():
    return "ok"


# ---------- static pages ----------

@app.get("/flash", include_in_schema=False)
def flash_redirect():
    return RedirectResponse("/flash/")


@app.get("/admin", include_in_schema=False)
def admin_redirect():
    return RedirectResponse("/admin/")


@app.middleware("http")
async def revalidate(request, call_next):
    response = await call_next(request)
    response.headers.setdefault("Cache-Control", "no-cache")  # browsers recheck the ETag, so a deploy shows up on a plain reload
    return response


app.mount("/admin/", StaticFiles(directory=os.path.join(STATIC, "admin"), html=True), name="admin")
app.mount("/flash/", StaticFiles(directory=os.path.join(STATIC, "flash"), html=True), name="flash")
app.mount("/", StaticFiles(directory=STATIC, html=True), name="static")
