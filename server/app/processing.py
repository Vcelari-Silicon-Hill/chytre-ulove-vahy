"""Turning raw readings into what a beekeeper wants to see.

"Net weight" removes the jumps the beekeeper causes (adding a super, harvesting,
an inspection) so the curve shows only what the colony did: nectar flow, consumption,
swarming. Raw data are always kept, so this can be improved and recomputed any time.
"""
import os
from datetime import datetime
from zoneinfo import ZoneInfo

TZ = ZoneInfo(os.environ.get("TZ_NAME", "Europe/Prague"))

STEP_KG = 3.0            # a change this big between two readings is the beekeeper, not the bees
MAX_STEP_GAP_S = 35 * 60 # ...but only if the two readings are close in time
SWARM_MIN_KG = 0.8       # a swarm takes roughly 1-3 kg of bees
SWARM_MONTHS = range(4, 8)
SWARM_HOURS = range(9, 18)


def _is_swarm_time(ts: int) -> bool:
    t = datetime.fromtimestamp(ts, TZ)
    return t.month in SWARM_MONTHS and t.hour in SWARM_HOURS


def net_weight(ts: list[int], kg: list[float | None]) -> tuple[list[float | None], list[dict]]:
    """Return the weight without manipulation jumps, and the list of detected events.

    The curve is anchored at the end: the last net value equals the last measured weight,
    earlier values are shifted by every jump that happened after them.
    """
    net: list[float | None] = []
    events: list[dict] = []
    offset = 0.0
    prev_t = prev_k = None
    for t, k in zip(ts, kg):
        if k is None:
            net.append(None)
            continue
        if prev_k is not None and t - prev_t <= MAX_STEP_GAP_S:
            d = k - prev_k
            if abs(d) >= STEP_KG:
                offset += d
                events.append({"ts": t, "delta": round(d, 2), "kind": "manipulace+" if d > 0 else "manipulace-"})
            elif -STEP_KG < d <= -SWARM_MIN_KG and _is_swarm_time(t):
                events.append({"ts": t, "delta": round(d, 2), "kind": "roj?"})
        net.append(k - offset)
        prev_t, prev_k = t, k
    net = [None if v is None else round(v + offset, 3) for v in net]
    return net, events


def downsample(columns: dict[str, list], max_points: int) -> dict[str, list]:
    """Average neighbouring points so a long range stays light for the browser."""
    n = len(columns["ts"])
    if n <= max_points:
        return columns
    step = -(-n // max_points)
    out: dict[str, list] = {k: [] for k in columns}
    for i in range(0, n, step):
        for key, values in columns.items():
            chunk = [v for v in values[i:i + step] if v is not None]
            if not chunk:
                out[key].append(None)
            elif key == "ts":
                out[key].append(int(sum(chunk) / len(chunk)))
            else:
                out[key].append(round(sum(chunk) / len(chunk), 3))
    return out


def change_over(ts: list[int], values: list[float | None], seconds: int) -> float | None:
    """Difference between the last value and the first value inside the last `seconds`."""
    pts = [(t, v) for t, v in zip(ts, values) if v is not None]
    if len(pts) < 2:
        return None
    last_t, last_v = pts[-1]
    start = next(((t, v) for t, v in pts if t >= last_t - seconds), None)
    if start is None or last_t - start[0] < seconds * 0.5:
        return None
    return round(last_v - start[1], 2)


def battery_percent(v: float | None) -> int | None:
    """Rough state of charge of a Li-ion cell from its resting voltage."""
    if v is None:
        return None
    table = [(3.30, 0), (3.50, 5), (3.65, 15), (3.75, 30), (3.82, 45), (3.90, 60), (4.00, 75), (4.10, 90), (4.18, 100)]
    if v <= table[0][0]:
        return 0
    for (v1, p1), (v2, p2) in zip(table, table[1:]):
        if v <= v2:
            return int(round(p1 + (p2 - p1) * (v - v1) / (v2 - v1)))
    return 100
