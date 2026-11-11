"""SQLite storage. One file, no external database needed."""
import hashlib
import os
import secrets
import sqlite3
import time
from contextlib import contextmanager

DB_PATH = os.environ.get("DB_PATH", os.path.join(os.path.dirname(__file__), "..", "data", "vahy.db"))

SCHEMA = """
CREATE TABLE IF NOT EXISTS scales (
    id          TEXT PRIMARY KEY,
    name        TEXT NOT NULL,
    location    TEXT NOT NULL DEFAULT '',
    token_hash  TEXT NOT NULL,
    created_at  INTEGER NOT NULL,
    public      INTEGER NOT NULL DEFAULT 1,
    last_seen   INTEGER,
    fw          TEXT,
    mac         TEXT,
    rssi        INTEGER,
    fails       INTEGER
);
CREATE TABLE IF NOT EXISTS readings (
    scale_id  TEXT NOT NULL REFERENCES scales(id) ON DELETE CASCADE,
    ts        INTEGER NOT NULL,
    raw       INTEGER,
    kg        REAL,
    t_cell    REAL,
    t_box     REAL,
    vbat      REAL,
    PRIMARY KEY (scale_id, ts)
) WITHOUT ROWID;
"""


def connect() -> sqlite3.Connection:
    os.makedirs(os.path.dirname(os.path.abspath(DB_PATH)), exist_ok=True)
    con = sqlite3.connect(DB_PATH, timeout=10)
    con.row_factory = sqlite3.Row
    con.execute("PRAGMA journal_mode=WAL")
    con.execute("PRAGMA foreign_keys=ON")
    return con


@contextmanager
def db():
    con = connect()
    try:
        yield con
        con.commit()
    finally:
        con.close()


def init() -> None:
    with db() as con:
        con.executescript(SCHEMA)


def hash_token(token: str) -> str:
    return hashlib.sha256(token.encode()).hexdigest()


def new_token() -> str:
    return secrets.token_urlsafe(24)


def add_scale(scale_id: str, name: str, location: str = "") -> str:
    token = new_token()
    with db() as con:
        con.execute(
            "INSERT INTO scales (id, name, location, token_hash, created_at) VALUES (?,?,?,?,?)",
            (scale_id, name, location, hash_token(token), int(time.time())),
        )
    return token


def rotate_token(scale_id: str) -> str:
    token = new_token()
    with db() as con:
        cur = con.execute("UPDATE scales SET token_hash=? WHERE id=?", (hash_token(token), scale_id))
        if cur.rowcount == 0:
            raise KeyError(scale_id)
    return token
