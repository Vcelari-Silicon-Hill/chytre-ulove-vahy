import os
import tempfile

os.environ["DB_PATH"] = os.path.join(tempfile.mkdtemp(), "test.db")

from fastapi.testclient import TestClient  # noqa: E402

from app import db  # noqa: E402
from app.main import app  # noqa: E402
from app.processing import net_weight, battery_percent  # noqa: E402


def client():
    c = TestClient(app)
    c.__enter__()
    return c


def test_ingest_and_read():
    c = client()
    token = db.add_scale("t1", "Test")
    body = {"scale": "t1", "fw": "1.0.0", "readings": [
        {"age": 1800, "raw": 1000, "kg": 40.0, "tc": 5.0, "tb": 6.0, "vb": 3.9},
        {"age": 900, "raw": 1010, "kg": 40.1, "tc": 5.1, "tb": 6.1, "vb": 3.9},
        {"age": 0, "raw": 1500, "kg": 46.1, "tc": 5.2, "tb": 6.2, "vb": 3.9},
    ]}
    assert c.post("/api/v1/ingest", json=body).status_code == 401
    assert c.post("/api/v1/ingest", json=body, headers={"Authorization": "Bearer nope"}).status_code == 403
    r = c.post("/api/v1/ingest", json=body, headers={"Authorization": f"Bearer {token}"})
    assert r.status_code == 200 and r.json()["stored"] == 3
    s = c.get("/api/v1/scales").json()["scales"][0]
    assert s["id"] == "t1" and s["status"] == "ok" and s["kg"] == 46.1
    ser = c.get("/api/v1/scales/t1/series?days=1").json()
    assert ser["net"][-1] == 46.1 and ser["net"][0] == 46.0   # the +6 kg super does not count
    assert ser["events"][0]["kind"] == "manipulace+"
    assert c.get("/api/v1/scales/t1/csv").text.startswith("time_utc")
    assert c.get("/api/v1/scales/nope/series").status_code == 404


def test_wrong_scale_for_token():
    c = client()
    token = db.add_scale("t2", "Test 2")
    r = c.post("/api/v1/ingest", json={"scale": "t1", "readings": []}, headers={"Authorization": f"Bearer {token}"})
    assert r.status_code == 403


def test_processing():
    net, ev = net_weight([0, 900, 1800, 2700], [30.0, 30.1, 15.0, 30.2])
    assert net == [30.1, 30.2, 30.2, 30.2] and len(ev) == 2  # inspection: super off and back on
    assert battery_percent(4.2) == 100 and battery_percent(3.2) == 0
