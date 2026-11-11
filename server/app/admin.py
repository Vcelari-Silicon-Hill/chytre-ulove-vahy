"""Admin tool: register scales and issue their secret tokens.

    python -m app.admin add vaha-1 "Úl 1" --location "Strahov, u bloku 7"
    python -m app.admin list
    python -m app.admin token vaha-1          # new token (the old one stops working)
    python -m app.admin rename vaha-1 "Úl 1 – Kraňská"
    python -m app.admin hide vaha-1 / show vaha-1
    python -m app.admin delete vaha-1         # deletes the scale and all its data
"""
import argparse
import re
import sys
import time

from . import db


def main(argv=None) -> int:
    p = argparse.ArgumentParser(prog="python -m app.admin", description="Správa vah")
    sub = p.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("add", help="přidat váhu a vypsat její token")
    a.add_argument("id", help="krátké ID, např. vaha-1 (malá písmena, číslice, pomlčka)")
    a.add_argument("name", help="název na dashboardu, např. 'Úl 1'")
    a.add_argument("--location", default="")
    sub.add_parser("list", help="vypsat váhy")
    t = sub.add_parser("token", help="vydat nový token (starý přestane platit)")
    t.add_argument("id")
    r = sub.add_parser("rename", help="přejmenovat")
    r.add_argument("id")
    r.add_argument("name")
    r.add_argument("--location")
    for name in ("hide", "show", "delete"):
        x = sub.add_parser(name)
        x.add_argument("id")
    args = p.parse_args(argv)
    db.init()

    if args.cmd == "add":
        if not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,31}", args.id):
            print("ID smí obsahovat jen malá písmena, číslice a pomlčky.", file=sys.stderr)
            return 2
        try:
            token = db.add_scale(args.id, args.name, args.location)
        except Exception as e:  # sqlite3.IntegrityError
            print(f"Nelze přidat: {e}", file=sys.stderr)
            return 1
        print(f"Váha {args.id} přidána.\n  scale_id: {args.id}\n  token:    {token}\nToken si uložte, znovu ho nevypíšu.")
    elif args.cmd == "list":
        with db.db() as con:
            rows = con.execute(
                "SELECT s.*, (SELECT COUNT(*) FROM readings r WHERE r.scale_id=s.id) AS n FROM scales s ORDER BY id"
            ).fetchall()
        for s in rows:
            seen = time.strftime("%Y-%m-%d %H:%M", time.localtime(s["last_seen"])) if s["last_seen"] else "nikdy"
            vis = "" if s["public"] else " [skrytá]"
            print(f"{s['id']:<12} {s['name']:<24} měření: {s['n']:<7} naposledy: {seen}  fw {s['fw'] or '-'}  MAC {s['mac'] or '-'}{vis}")
    elif args.cmd == "token":
        try:
            print(f"Nový token pro {args.id}: {db.rotate_token(args.id)}")
        except KeyError:
            print("Taková váha neexistuje.", file=sys.stderr)
            return 1
    else:
        with db.db() as con:
            if args.cmd == "rename":
                cur = con.execute("UPDATE scales SET name=?, location=COALESCE(?, location) WHERE id=?",
                                  (args.name, args.location, args.id))
            elif args.cmd in ("hide", "show"):
                cur = con.execute("UPDATE scales SET public=? WHERE id=?", (1 if args.cmd == "show" else 0, args.id))
            else:
                cur = con.execute("DELETE FROM scales WHERE id=?", (args.id,))
            if cur.rowcount == 0:
                print("Taková váha neexistuje.", file=sys.stderr)
                return 1
        print("Hotovo.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
