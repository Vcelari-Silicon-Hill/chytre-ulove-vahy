"""Fills a server with realistic made-up data, for trying the dashboard before the real scales run.

    python -m tools.simulate --url http://localhost:8000 --scales 3 --days 21

Creates scales demo-1 … demo-N (directly in the database, so run it next to the server's data)
and uploads the readings through the normal API, exactly as a scale would.
Remove them afterwards with:  python -m app.admin delete demo-1
"""
import argparse
import json
import math
import random
import time
import urllib.request

from app import db


def make_series(days: int, seed: int, step_s: int = 900):
    rnd = random.Random(seed)
    now = int(time.time())
    start = now - days * 86400
    kg = 38.0 + rnd.uniform(-6, 6)
    vbat = 3.95
    out = []
    manip_at = {start + int(days * 86400 * f) for f in (0.35, 0.7)}
    for t in range(start, now, step_s):
        hour = (t % 86400) / 3600 + 1  # roughly local time in winter
        day_phase = math.sin((hour - 9) / 24 * 2 * math.pi)
        t_out = 4 + 5 * day_phase + rnd.gauss(0, 0.3)
        kg -= 0.07 / 96 + rnd.gauss(0, 0.002)            # winter consumption ~70 g/day
        if any(abs(t - m) < step_s / 2 for m in manip_at):
            kg += rnd.choice([5.5, -4.2])                    # feeding or an inspection
        drift = 0.012 * (t_out - 4)                           # residual temperature effect
        vbat += (0.0009 if 9 <= hour <= 15 else 0) - 0.00012
        vbat = min(4.18, max(3.5, vbat))
        out.append({
            "age": now - t,
            "raw": int(kg * 21500 + 84000),
            "kg": round(kg + drift + rnd.gauss(0, 0.01), 3),
            "tc": round(t_out + 1.5, 2),
            "tb": round(t_out + 2.5 + 2 * max(0, day_phase), 2),
            "vb": round(vbat, 3),
        })
    return out


def post(url: str, token: str, body: dict) -> int:
    req = urllib.request.Request(url, data=json.dumps(body).encode(), method="POST",
                                 headers={"Content-Type": "application/json", "Authorization": f"Bearer {token}"})
    with urllib.request.urlopen(req) as r:
        return r.status


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--url", default="http://localhost:8000")
    p.add_argument("--scales", type=int, default=3)
    p.add_argument("--days", type=int, default=21)
    a = p.parse_args()
    db.init()
    names = ["Úl 1 – Kraňka", "Úl 2 – Buckfast", "Úl 3 – oddělek", "Úl 4", "Úl 5", "Úl 6", "Úl 7"]
    for i in range(1, a.scales + 1):
        sid = f"demo-{i}"
        try:
            token = db.add_scale(sid, names[i - 1] + " (ukázka)", "Strahov, ukázková data")
        except Exception:
            token = db.rotate_token(sid)
        readings = make_series(a.days, seed=i)
        for k in range(0, len(readings), 96):
            post(f"{a.url}/api/v1/ingest", token, {"scale": sid, "fw": "sim", "rssi": -60 - i * 4, "readings": readings[k:k + 96]})
        print(f"{sid}: {len(readings)} měření")


if __name__ == "__main__":
    main()
