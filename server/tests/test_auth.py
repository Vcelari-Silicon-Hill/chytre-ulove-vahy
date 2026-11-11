import os
import tempfile

os.environ["DB_PATH"] = os.path.join(tempfile.mkdtemp(), "auth.db")
os.environ["ADMIN_EMAIL"] = "admin@example.org"

from fastapi.testclient import TestClient  # noqa: E402

import app.auth as auth  # noqa: E402
from app import db  # noqa: E402
from app.main import app  # noqa: E402

H = {"X-Requested-With": "fetch"}


def token_from(link: str) -> str:
    return link.split("#heslo=")[1]


def setup_client(link: str, password: str) -> TestClient:
    c = TestClient(app)
    assert c.get(f"/api/v1/auth/invite?token={token_from(link)}").json()["email"]
    r = c.post("/api/v1/auth/setup", json={"token": token_from(link), "password": password}, headers=H)
    assert r.status_code == 200, r.text
    return c


def test_full_flow():
    with TestClient(app):
        pass  # runs startup: creates the first admin from ADMIN_EMAIL
    with db.db() as con:
        uid = con.execute("SELECT id FROM users WHERE email='admin@example.org'").fetchone()["id"]
    admin = setup_client(auth.setup_link(auth.create_invite(uid), "https://x"), "spravne-heslo-1")
    assert admin.get("/api/v1/auth/me").json()["role"] == "admin"

    # link is single use
    anon = TestClient(app)
    assert anon.get("/api/v1/admin/scales").status_code == 401
    assert anon.post("/api/v1/auth/login", json={"email": "admin@example.org", "password": "x"}, headers=H).status_code == 401
    assert anon.post("/api/v1/auth/login", json={"email": "admin@example.org", "password": "spravne-heslo-1"}).status_code == 400
    assert anon.post("/api/v1/auth/login", json={"email": "ADMIN@example.org", "password": "spravne-heslo-1"}, headers=H).status_code == 200

    # scales
    r = admin.post("/api/v1/admin/scales", json={"id": "vaha-1", "name": "Úl 1"}, headers=H)
    assert r.status_code == 200 and len(r.json()["token"]) > 20
    assert admin.post("/api/v1/admin/scales", json={"id": "vaha-1", "name": "x"}, headers=H).status_code == 409
    assert admin.post("/api/v1/admin/scales", json={"id": "Bad ID", "name": "x"}, headers=H).status_code == 400
    assert admin.patch("/api/v1/admin/scales/vaha-1", json={"public": False, "name": "Úl jedna"}, headers=H).status_code == 200
    scales = {s["id"]: s for s in admin.get("/api/v1/admin/scales").json()["scales"]}
    assert scales["vaha-1"]["name"] == "Úl jedna" and scales["vaha-1"]["public"] == 0
    assert admin.post("/api/v1/admin/scales/vaha-1/token", headers=H).status_code == 200

    # invite an editor
    r = admin.post("/api/v1/admin/users", json={"email": "editor@example.org", "role": "editor"}, headers=H)
    assert r.status_code == 200
    editor = setup_client(r.json()["link"], "editor-heslo-1")
    assert editor.post("/api/v1/auth/setup", json={"token": token_from(r.json()["link"]), "password": "zzzzzzzzzzzz"}, headers=H).status_code == 404
    assert editor.get("/api/v1/admin/users").status_code == 403
    assert editor.delete("/api/v1/admin/scales/vaha-1", headers=H).status_code == 403
    assert editor.post("/api/v1/admin/scales", json={"id": "vaha-2", "name": "Úl 2"}, headers=H).status_code == 200

    # the last admin cannot be removed or demoted, nobody can remove themselves
    me = admin.get("/api/v1/auth/me").json()["id"]
    assert admin.delete(f"/api/v1/admin/users/{me}", headers=H).status_code == 400
    assert admin.patch(f"/api/v1/admin/users/{me}", json={"role": "editor"}, headers=H).status_code == 400

    # password change and logout
    assert editor.post("/api/v1/auth/password", json={"old": "editor-heslo-1", "new": "krátké"}, headers=H).status_code == 422
    assert editor.post("/api/v1/auth/password", json={"old": "editor-heslo-1", "new": "nove-heslo-123"}, headers=H).status_code == 200
    assert editor.post("/api/v1/auth/logout", headers=H).status_code == 200
    assert editor.get("/api/v1/auth/me").status_code == 401

    users = admin.get("/api/v1/admin/users").json()["users"]
    eid = [u for u in users if u["email"] == "editor@example.org"][0]["id"]
    assert admin.delete(f"/api/v1/admin/users/{eid}", headers=H).status_code == 200


def test_password_hash():
    h = auth.hash_password("tajne-heslo-123")
    assert auth.check_password("tajne-heslo-123", h)
    assert not auth.check_password("jine-heslo", h)
    assert not auth.check_password("x", None)
