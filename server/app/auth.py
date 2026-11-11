"""People who manage the scales: invitations, passwords, sessions and the admin API.

Nobody signs up. The first admin comes from the ADMIN_EMAIL setting (or the admin CLI);
admins invite others by email. An invitation is a one-time link to set a password; the
app shows the link and a ready-made email text, the admin sends it themselves.

Roles: "admin" manages scales and people, "editor" manages scales only.
"""
import base64
import hashlib
import hmac
import os
import re
import secrets
import time
from collections import defaultdict, deque
from typing import Optional

from fastapi import APIRouter, Cookie, Depends, Header, HTTPException, Request, Response
from pydantic import BaseModel, Field

from . import db

PUBLIC_URL = os.environ.get("PUBLIC_URL", "").rstrip("/")
SESSION_COOKIE = "vahy_session"
SESSION_DAYS = 30
INVITE_DAYS = 7
MIN_PASSWORD = 10
EMAIL_RE = re.compile(r"^[^@\s]+@[^@\s]+\.[^@\s]+$")

router = APIRouter(prefix="/api/v1")


# ---------- passwords and tokens ----------

def hash_password(password: str) -> str:
    salt = secrets.token_bytes(16)
    n, r, p = 2**14, 8, 1
    dk = hashlib.scrypt(password.encode(), salt=salt, n=n, r=r, p=p, dklen=32)
    return f"scrypt${n}${r}${p}${base64.b64encode(salt).decode()}${base64.b64encode(dk).decode()}"


def check_password(password: str, stored: Optional[str]) -> bool:
    if not stored:
        return False
    try:
        _, n, r, p, salt, dk = stored.split("$")
        got = hashlib.scrypt(password.encode(), salt=base64.b64decode(salt), n=int(n), r=int(r), p=int(p), dklen=32)
        return hmac.compare_digest(got, base64.b64decode(dk))
    except Exception:
        return False


def _hash(token: str) -> str:
    return hashlib.sha256(token.encode()).hexdigest()


def create_user(email: str, role: str, invited_by: Optional[int] = None) -> int:
    with db.db() as con:
        cur = con.execute("INSERT INTO users (email, role, created_at, invited_by) VALUES (?,?,?,?)",
                          (email.strip(), role, int(time.time()), invited_by))
        return cur.lastrowid


def create_invite(user_id: int) -> str:
    """One-time link token to set (or reset) the password. Older unused links stop working."""
    token = secrets.token_urlsafe(32)
    with db.db() as con:
        con.execute("UPDATE invites SET used=1 WHERE user_id=? AND used=0", (user_id,))
        con.execute("INSERT INTO invites (token_hash, user_id, expires_at) VALUES (?,?,?)",
                    (_hash(token), user_id, int(time.time()) + INVITE_DAYS * 86400))
    return token


def setup_link(token: str, base: str = "") -> str:
    return f"{(base or PUBLIC_URL).rstrip('/')}/admin/#heslo={token}"


def ensure_first_admin() -> Optional[str]:
    """Create the ADMIN_EMAIL user on first start. Returns a setup link if one was made."""
    email = os.environ.get("ADMIN_EMAIL", "").strip()
    if not email or not EMAIL_RE.match(email):
        return None
    with db.db() as con:
        row = con.execute("SELECT id, pw_hash FROM users WHERE email=?", (email,)).fetchone()
    if row is None:
        uid = create_user(email, "admin")
    elif row["pw_hash"] is None:
        uid = row["id"]
    else:
        return None
    return setup_link(create_invite(uid))


# ---------- sessions ----------

class User(BaseModel):
    id: int
    email: str
    role: str


def _base_url(request: Request) -> str:
    return PUBLIC_URL or str(request.base_url).rstrip("/")


def current_user(vahy_session: Optional[str] = Cookie(default=None)) -> User:
    if not vahy_session:
        raise HTTPException(401, "Nejste přihlášeni.")
    with db.db() as con:
        row = con.execute(
            "SELECT u.id, u.email, u.role FROM sessions s JOIN users u ON u.id=s.user_id "
            "WHERE s.token_hash=? AND s.expires_at>?", (_hash(vahy_session), int(time.time()))
        ).fetchone()
    if row is None:
        raise HTTPException(401, "Přihlášení vypršelo.")
    return User(**dict(row))


def require_admin(user: User = Depends(current_user)) -> User:
    if user.role != "admin":
        raise HTTPException(403, "Na tohle jsou potřeba práva správce.")
    return user


def same_origin(x_requested_with: Optional[str] = Header(default=None)) -> None:
    """Changing requests must come from our own page (a cross-site form cannot set this header)."""
    if x_requested_with != "fetch":
        raise HTTPException(400, "Missing X-Requested-With header.")


def _start_session(response: Response, request: Request, user_id: int) -> None:
    token = secrets.token_urlsafe(32)
    now = int(time.time())
    with db.db() as con:
        con.execute("DELETE FROM sessions WHERE expires_at<?", (now,))
        con.execute("INSERT INTO sessions (token_hash, user_id, created_at, expires_at) VALUES (?,?,?,?)",
                    (_hash(token), user_id, now, now + SESSION_DAYS * 86400))
        con.execute("UPDATE users SET last_login=? WHERE id=?", (now, user_id))
    response.set_cookie(SESSION_COOKIE, token, max_age=SESSION_DAYS * 86400, httponly=True, samesite="lax",
                        secure=request.url.scheme == "https", path="/")


# simple brake on password guessing: 10 failures per 15 minutes per address
_failures: dict[str, deque] = defaultdict(deque)


def _too_many(key: str) -> bool:
    q = _failures[key]
    while q and q[0] < time.time() - 900:
        q.popleft()
    return len(q) >= 10


# ---------- auth endpoints ----------

class Login(BaseModel):
    email: str
    password: str


@router.post("/auth/login", dependencies=[Depends(same_origin)])
def login(body: Login, request: Request, response: Response):
    key = request.client.host if request.client else "?"
    if _too_many(key):
        raise HTTPException(429, "Příliš mnoho pokusů, zkuste to za 15 minut.")
    with db.db() as con:
        row = con.execute("SELECT id, pw_hash FROM users WHERE email=?", (body.email.strip(),)).fetchone()
    if row is None or not check_password(body.password, row["pw_hash"]):
        _failures[key].append(time.time())
        raise HTTPException(401, "Špatný e-mail nebo heslo.")
    _start_session(response, request, row["id"])
    return {"ok": True}


@router.post("/auth/logout", dependencies=[Depends(same_origin)])
def logout(response: Response, vahy_session: Optional[str] = Cookie(default=None)):
    if vahy_session:
        with db.db() as con:
            con.execute("DELETE FROM sessions WHERE token_hash=?", (_hash(vahy_session),))
    response.delete_cookie(SESSION_COOKIE, path="/")
    return {"ok": True}


@router.get("/auth/me")
def me(user: User = Depends(current_user)):
    return user


@router.get("/auth/invite")
def invite_info(token: str):
    with db.db() as con:
        row = con.execute(
            "SELECT u.email, u.pw_hash FROM invites i JOIN users u ON u.id=i.user_id "
            "WHERE i.token_hash=? AND i.used=0 AND i.expires_at>?", (_hash(token), int(time.time()))
        ).fetchone()
    if row is None:
        raise HTTPException(404, "Odkaz je neplatný nebo už vypršel. Požádejte o nový.")
    return {"email": row["email"], "reset": row["pw_hash"] is not None}


class SetPassword(BaseModel):
    token: str
    password: str = Field(min_length=MIN_PASSWORD, max_length=200)


@router.post("/auth/setup", dependencies=[Depends(same_origin)])
def setup_password(body: SetPassword, request: Request, response: Response):
    now = int(time.time())
    with db.db() as con:
        row = con.execute("SELECT user_id FROM invites WHERE token_hash=? AND used=0 AND expires_at>?",
                          (_hash(body.token), now)).fetchone()
        if row is None:
            raise HTTPException(404, "Odkaz je neplatný nebo už vypršel. Požádejte o nový.")
        con.execute("UPDATE invites SET used=1 WHERE token_hash=?", (_hash(body.token),))
        con.execute("UPDATE users SET pw_hash=? WHERE id=?", (hash_password(body.password), row["user_id"]))
        con.execute("DELETE FROM sessions WHERE user_id=?", (row["user_id"],))  # log out everywhere else
    _start_session(response, request, row["user_id"])
    return {"ok": True}


class ChangePassword(BaseModel):
    old: str
    new: str = Field(min_length=MIN_PASSWORD, max_length=200)


@router.post("/auth/password", dependencies=[Depends(same_origin)])
def change_password(body: ChangePassword, user: User = Depends(current_user)):
    with db.db() as con:
        row = con.execute("SELECT pw_hash FROM users WHERE id=?", (user.id,)).fetchone()
        if not check_password(body.old, row["pw_hash"]):
            raise HTTPException(400, "Současné heslo nesouhlasí.")
        con.execute("UPDATE users SET pw_hash=? WHERE id=?", (hash_password(body.new), user.id))
    return {"ok": True}


# ---------- scales ----------

SCALE_ID_RE = re.compile(r"^[a-z0-9][a-z0-9-]{0,31}$")


@router.get("/admin/scales")
def admin_scales(user: User = Depends(current_user)):
    with db.db() as con:
        rows = con.execute(
            "SELECT s.id, s.name, s.location, s.public, s.created_at, s.last_seen, s.fw, s.mac, s.rssi, s.fails, "
            "(SELECT COUNT(*) FROM readings r WHERE r.scale_id=s.id) AS readings "
            "FROM scales s ORDER BY s.id"
        ).fetchall()
    return {"scales": [dict(r) for r in rows], "now": int(time.time())}


class NewScale(BaseModel):
    id: str
    name: str = Field(min_length=1, max_length=80)
    location: str = Field(default="", max_length=120)


@router.post("/admin/scales", dependencies=[Depends(same_origin)])
def admin_add_scale(body: NewScale, user: User = Depends(current_user)):
    if not SCALE_ID_RE.match(body.id):
        raise HTTPException(400, "ID smí obsahovat jen malá písmena, číslice a pomlčky (např. vaha-1).")
    try:
        token = db.add_scale(body.id, body.name.strip(), body.location.strip())
    except Exception:
        raise HTTPException(409, "Váha s tímto ID už existuje.")
    return {"id": body.id, "token": token}


class EditScale(BaseModel):
    name: Optional[str] = Field(default=None, min_length=1, max_length=80)
    location: Optional[str] = Field(default=None, max_length=120)
    public: Optional[bool] = None


@router.patch("/admin/scales/{scale_id}", dependencies=[Depends(same_origin)])
def admin_edit_scale(scale_id: str, body: EditScale, user: User = Depends(current_user)):
    with db.db() as con:
        cur = con.execute(
            "UPDATE scales SET name=COALESCE(?,name), location=COALESCE(?,location), public=COALESCE(?,public) WHERE id=?",
            (body.name, body.location, None if body.public is None else int(body.public), scale_id),
        )
        if cur.rowcount == 0:
            raise HTTPException(404, "Váha nenalezena.")
    return {"ok": True}


@router.post("/admin/scales/{scale_id}/token", dependencies=[Depends(same_origin)])
def admin_new_token(scale_id: str, user: User = Depends(current_user)):
    try:
        return {"id": scale_id, "token": db.rotate_token(scale_id)}
    except KeyError:
        raise HTTPException(404, "Váha nenalezena.")


@router.delete("/admin/scales/{scale_id}", dependencies=[Depends(same_origin)])
def admin_delete_scale(scale_id: str, user: User = Depends(require_admin)):
    with db.db() as con:
        cur = con.execute("DELETE FROM scales WHERE id=?", (scale_id,))
        if cur.rowcount == 0:
            raise HTTPException(404, "Váha nenalezena.")
    return {"ok": True}


# ---------- people ----------

@router.get("/admin/users")
def admin_users(user: User = Depends(require_admin)):
    now = int(time.time())
    with db.db() as con:
        rows = con.execute(
            "SELECT u.id, u.email, u.role, u.created_at, u.last_login, u.pw_hash IS NOT NULL AS active, "
            "(SELECT MAX(expires_at) FROM invites i WHERE i.user_id=u.id AND i.used=0 AND i.expires_at>?) AS invite_expires, "
            "(SELECT email FROM users x WHERE x.id=u.invited_by) AS invited_by "
            "FROM users u ORDER BY u.created_at", (now,)
        ).fetchall()
    return {"users": [dict(r) for r in rows], "me": user.id}


class Invite(BaseModel):
    email: str = Field(max_length=200)
    role: str = Field(pattern="^(admin|editor)$")


@router.post("/admin/users", dependencies=[Depends(same_origin)])
def admin_invite(body: Invite, request: Request, user: User = Depends(require_admin)):
    email = body.email.strip()
    if not EMAIL_RE.match(email):
        raise HTTPException(400, "Neplatný e-mail.")
    try:
        uid = create_user(email, body.role, user.id)
    except Exception:
        raise HTTPException(409, "Tento e-mail už pozvaný je. Pošlete mu nový odkaz.")
    return {"id": uid, "email": email, "link": setup_link(create_invite(uid), _base_url(request)), "days": INVITE_DAYS}


@router.post("/admin/users/{user_id}/link", dependencies=[Depends(same_origin)])
def admin_new_link(user_id: int, request: Request, user: User = Depends(require_admin)):
    with db.db() as con:
        row = con.execute("SELECT email, pw_hash FROM users WHERE id=?", (user_id,)).fetchone()
    if row is None:
        raise HTTPException(404, "Uživatel nenalezen.")
    return {"id": user_id, "email": row["email"], "reset": row["pw_hash"] is not None,
            "link": setup_link(create_invite(user_id), _base_url(request)), "days": INVITE_DAYS}


class EditUser(BaseModel):
    role: str = Field(pattern="^(admin|editor)$")


def _admins_left(con, excluding: int) -> int:
    return con.execute("SELECT COUNT(*) FROM users WHERE role='admin' AND id<>?", (excluding,)).fetchone()[0]


@router.patch("/admin/users/{user_id}", dependencies=[Depends(same_origin)])
def admin_edit_user(user_id: int, body: EditUser, user: User = Depends(require_admin)):
    with db.db() as con:
        if body.role != "admin" and _admins_left(con, user_id) == 0:
            raise HTTPException(400, "Musí zůstat aspoň jeden správce.")
        cur = con.execute("UPDATE users SET role=? WHERE id=?", (body.role, user_id))
        if cur.rowcount == 0:
            raise HTTPException(404, "Uživatel nenalezen.")
    return {"ok": True}


@router.delete("/admin/users/{user_id}", dependencies=[Depends(same_origin)])
def admin_remove_user(user_id: int, user: User = Depends(require_admin)):
    if user_id == user.id:
        raise HTTPException(400, "Sami sebe odebrat nemůžete.")
    with db.db() as con:
        if _admins_left(con, user_id) == 0:
            raise HTTPException(400, "Musí zůstat aspoň jeden správce.")
        cur = con.execute("DELETE FROM users WHERE id=?", (user_id,))
        if cur.rowcount == 0:
            raise HTTPException(404, "Uživatel nenalezen.")
    return {"ok": True}
