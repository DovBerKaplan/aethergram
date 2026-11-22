#!/usr/bin/env python3
"""Turn an existing MTProto user session into an Aethergram import file.

Reads a session (auth key) held by tg-session-gateway or any Pyrogram /
Telethon client and writes `$XDG_CONFIG_HOME/aethergram/import-session.json`
(default `~/.config/aethergram/import-session.json`), mode 600.  Aethergram
reads that file once on start (while not logged in) and then wipes it.

The auth key never appears in argv, in shell history, in `ps` output or in
this script's own output.  Stdlib only.

Examples:
  tools/import-gateway-session.py --pyrogram-file ~/.pyrogram/gw.session \\
      --source-is-stopped
  tools/import-gateway-session.py --docker-volume tgsessions --session main \\
      --gateway-url https://gw.example:8081 --alias main
"""

import argparse
import base64
import binascii
import json
import os
import secrets
import sqlite3
import struct
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request

AUTH_KEY_LEN = 256

# Session strings: (struct format, field names).  Accepted when
# len(decoded) == struct.calcsize(fmt).
#   v2  (271 bytes): dc_id, api_id, test_mode, auth_key, user_id, is_bot
#   old (263/267 bytes): dc_id, test_mode, auth_key, user_id (I/Q), is_bot
PYROGRAM_STRING_FORMATS = [
    (">BI?256sQ?", ("dc_id", "api_id", "test_mode", "auth_key", "user_id", "is_bot")),
    (">B?256sQ?", ("dc_id", "test_mode", "auth_key", "user_id", "is_bot")),
    (">B?256sI?", ("dc_id", "test_mode", "auth_key", "user_id", "is_bot")),
]

# Telethon StringSession body after the leading version char '1'.
#   263 bytes: dc_id, ipv4 (4s), port, auth_key
#   275 bytes: dc_id, ipv6 (16s), port, auth_key
TELETHON_STRING_FORMATS = [">B4sH256s", ">B16sH256s"]

# Runs inside `python:3.12-alpine` with the docker volume mounted at /d:ro.
# Prints exactly one JSON line; the host side never echoes it.
DOCKER_SCRIPT = """
import base64, json, os, sqlite3, sys
name = sys.argv[1]
cands = ("sessions/" + name + ".session.session",
         "sessions/" + name + ".session",
         name + ".session")
path = None
for c in cands:
    p = os.path.join("/d", c)
    if os.path.exists(p):
        path = p
        break
if path is None:
    sys.stderr.write("no session file for %r in the volume\\n" % name)
    sys.exit(3)
con = sqlite3.connect("file:" + path + "?mode=ro", uri=True)
try:
    row = con.execute(
        "select dc_id, test_mode, auth_key, user_id, is_bot"
        " from sessions").fetchone()
finally:
    con.close()
if row is None:
    sys.stderr.write("sessions table is empty\\n")
    sys.exit(3)
dc_id, test_mode, auth_key, user_id, is_bot = row
if not isinstance(auth_key, (bytes, bytearray, memoryview)):
    sys.stderr.write("auth_key column is not a blob\\n")
    sys.exit(3)
print(json.dumps({
    "dc_id": dc_id,
    "test_mode": bool(test_mode),
    "user_id": user_id,
    "is_bot": bool(is_bot),
    "auth_key_b64": base64.b64encode(bytes(auth_key)).decode("ascii"),
}))
"""


def fail(msg):
    print("error: " + msg, file=sys.stderr)
    sys.exit(1)


def to_bool(v):
    if v is None:
        return False
    if isinstance(v, bool):
        return v
    try:
        return bool(int(v))
    except (TypeError, ValueError):
        return False


def as_key_bytes(v, what):
    if isinstance(v, memoryview):
        v = v.tobytes()
    if isinstance(v, bytearray):
        v = bytes(v)
    if isinstance(v, str):
        v = v.encode("latin-1")
    if not isinstance(v, bytes):
        fail("%s: auth_key is not a blob" % what)
    return v


def b64_decode(s, what):
    s = s.strip().replace("-", "+").replace("_", "/")
    s += "=" * (-len(s) % 4)
    try:
        return base64.b64decode(s, validate=True)
    except (binascii.Error, ValueError):
        fail("%s: not valid base64" % what)


def read_stdin(what):
    data = sys.stdin.read().strip()
    if not data:
        fail("no %s received on stdin (paste the session string, then Ctrl-D)"
             % what)
    return data


def sqlite_connect_ro(path):
    if not os.path.isfile(path):
        fail("no such file: %s" % path)
    uri = "file:" + urllib.parse.quote(path) + "?mode=ro"
    try:
        return sqlite3.connect(uri, uri=True)
    except sqlite3.Error as e:
        fail("cannot open %s read-only: %s" % (path, e))


# ---------------------------------------------------------------- sources

def from_pyrogram_db(path):
    con = sqlite_connect_ro(path)
    try:
        try:
            row = con.execute("select dc_id, test_mode, auth_key, user_id,"
                              " is_bot from sessions").fetchone()
        except sqlite3.Error as e:
            fail("%s is not a Pyrogram session database: %s" % (path, e))
    finally:
        con.close()
    if row is None:
        fail("%s: the sessions table is empty" % path)
    dc_id, test_mode, auth_key, user_id, is_bot = row
    return {"kind": "Pyrogram", "dc_id": dc_id, "test_mode": to_bool(test_mode),
            "auth_key": as_key_bytes(auth_key, path),
            "user_id": None if user_id is None else int(user_id),
            "is_bot": to_bool(is_bot)}


def from_telethon_db(path):
    con = sqlite_connect_ro(path)
    try:
        try:
            row = con.execute("select dc_id, auth_key from sessions").fetchone()
        except sqlite3.Error as e:
            fail("%s is not a Telethon session database: %s" % (path, e))
    finally:
        con.close()
    if row is None:
        fail("%s: the sessions table is empty" % path)
    dc_id, auth_key = row
    # Telethon sessions carry no user id -> --user-id is required.
    return {"kind": "Telethon", "dc_id": dc_id, "test_mode": False,
            "auth_key": as_key_bytes(auth_key, path),
            "user_id": None, "is_bot": False}


def from_pyrogram_string(s):
    raw = b64_decode(s, "pyrogram string")
    for fmt, fields in PYROGRAM_STRING_FORMATS:
        if len(raw) == struct.calcsize(fmt):
            d = dict(zip(fields, struct.unpack(fmt, raw)))
            return {"kind": "Pyrogram", "dc_id": d["dc_id"],
                    "test_mode": to_bool(d["test_mode"]),
                    "auth_key": d["auth_key"],
                    "user_id": None if d["user_id"] == 0 else int(d["user_id"]),
                    "is_bot": to_bool(d["is_bot"])}
    fail("input is not a known Pyrogram session string "
         "(%d bytes after base64 decoding)" % len(raw))


def from_telethon_string(s):
    if not s or s[0] != "1":
        fail("not a Telethon StringSession (expected leading version '1')")
    raw = b64_decode(s[1:], "telethon string")
    for fmt in TELETHON_STRING_FORMATS:
        if len(raw) == struct.calcsize(fmt):
            dc_id, ip, port, auth_key = struct.unpack(fmt, raw)
            return {"kind": "Telethon", "dc_id": dc_id, "test_mode": False,
                    "auth_key": auth_key, "user_id": None, "is_bot": False}
    fail("input is not a known Telethon StringSession "
         "(%d bytes after base64 decoding)" % len(raw))


def from_docker_volume(volume, session):
    cmd = ["docker", "run", "--rm", "-v", "%s:/d:ro" % volume,
           "python:3.12-alpine", "python", "-c", DOCKER_SCRIPT, session]
    try:
        proc = subprocess.run(cmd, capture_output=True, timeout=900)
    except FileNotFoundError:
        fail("docker is not on PATH; cannot read volume %r" % volume)
    except subprocess.TimeoutExpired:
        fail("docker run timed out while reading volume %r" % volume)
    if proc.returncode != 0:
        lines = proc.stderr.decode("utf-8", "replace").strip().splitlines()
        tail = lines[-1] if lines else "(no stderr)"
        fail("docker run failed (exit %d): %s" % (proc.returncode, tail))
    # stdout holds the auth key: parse it, never print it.
    lines = proc.stdout.decode("utf-8", "replace").strip().splitlines()
    if len(lines) != 1:
        fail("docker helper printed %d lines instead of one JSON line"
             % len(lines))
    try:
        obj = json.loads(lines[0])
        dc_id = int(obj["dc_id"])
        auth_key = base64.b64decode(obj["auth_key_b64"], validate=True)
        user_id = obj["user_id"]
    except (ValueError, KeyError, TypeError):
        fail("docker helper printed unexpected output")
    return {"kind": "Pyrogram", "dc_id": dc_id,
            "test_mode": to_bool(obj.get("test_mode")),
            "auth_key": auth_key,
            "user_id": None if user_id is None else int(user_id),
            "is_bot": to_bool(obj.get("is_bot"))}


# ---------------------------------------------------------------- guards

def pause_gateway(url, alias):
    secret = os.environ.get("GW_ADMIN_SECRET")
    if not secret:
        fail("GW_ADMIN_SECRET is not set; it must hold the tg-session-gateway"
             " admin secret (it is never printed)")
    endpoint = url.rstrip("/") + "/v1/admin/sessions/" + \
        urllib.parse.quote(alias, safe="") + "/pause"
    req = urllib.request.Request(
        endpoint, data=b"{}", method="POST",
        headers={"Authorization": "Bearer " + secret,
                 "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=20) as resp:
            code = resp.status
            resp.read()
    except urllib.error.HTTPError as e:
        fail("gateway refused to pause %s: HTTP %d; nothing was written"
             % (alias, e.code))
    except urllib.error.URLError as e:
        reason = getattr(e, "reason", e)
        fail("could not reach the gateway at %s (%s); the source may still"
             " be using this key, nothing was written" % (endpoint, reason))
    except OSError as e:
        fail("could not reach the gateway at %s (%s); nothing was written"
             % (endpoint, e))
    if not 200 <= code < 300:
        fail("gateway pause of %s returned HTTP %d; nothing was written"
             % (alias, code))


def default_out_path():
    xdg = os.environ.get("XDG_CONFIG_HOME")
    base = xdg if xdg and xdg.strip() else os.path.join(
        os.path.expanduser("~"), ".config")
    return os.path.join(base, "aethergram", "import-session.json")


def atomic_write_private(path, data):
    parent = os.path.dirname(os.path.abspath(path))
    if not os.path.isdir(parent):
        # create every missing component with mode 0700 (mkdir is subject
        # to umask, so chmod afterwards to be sure)
        missing = []
        p = parent
        while p and not os.path.isdir(p):
            missing.append(p)
            p = os.path.dirname(p)
        for d in reversed(missing):
            try:
                os.mkdir(d, 0o700)
            except FileExistsError:
                pass
            except OSError as e:
                fail("cannot create %s: %s" % (d, e))
            os.chmod(d, 0o700)
    for _ in range(100):
        tmp = os.path.join(parent, "." + os.path.basename(path) +
                           ".tmp." + secrets.token_hex(8))
        try:
            fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        except FileExistsError:
            continue
        except OSError as e:
            fail("cannot create a temp file in %s: %s" % (parent, e))
        try:
            with os.fdopen(fd, "wb") as f:
                f.write(data)
                f.flush()
                os.fsync(f.fileno())
        except BaseException:
            try:
                os.unlink(tmp)
            except OSError:
                pass
            raise
        os.replace(tmp, path)  # atomic; --force already checked by caller
        return
    fail("could not create a temporary file next to %s" % path)


def main():
    ap = argparse.ArgumentParser(
        description="Import a Pyrogram/Telethon (tg-session-gateway) session"
                    " into Aethergram.  Session strings are read from stdin,"
                    " never from the command line.")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--pyrogram-file", metavar="PATH",
                     help="Pyrogram SQLite session file")
    src.add_argument("--telethon-file", metavar="PATH",
                     help="Telethon SQLite session file")
    src.add_argument("--pyrogram-string", action="store_true",
                     help="read a Pyrogram session string from stdin")
    src.add_argument("--telethon-string", action="store_true",
                     help="read a Telethon StringSession from stdin")
    src.add_argument("--docker-volume", metavar="NAME",
                     help="docker volume holding a Pyrogram session")
    ap.add_argument("--session", metavar="NAME",
                    help="session name inside --docker-volume")
    ap.add_argument("--user-id", type=int, metavar="N",
                    help="user id (required for Telethon sources)")
    ap.add_argument("--source-is-stopped", action="store_true",
                    help="declare that the source client is no longer"
                         " running (AUTH_KEY_DUPLICATED guard)")
    ap.add_argument("--gateway-url", metavar="URL",
                    help="tg-session-gateway base URL; the session is paused"
                         " via its admin API before anything is written")
    ap.add_argument("--alias", metavar="ALIAS",
                    help="session alias in the gateway (with --gateway-url)")
    ap.add_argument("--out", metavar="PATH",
                    help="output path (default:"
                         " $XDG_CONFIG_HOME/aethergram/import-session.json)")
    ap.add_argument("--force", action="store_true",
                    help="overwrite an existing output file")
    args = ap.parse_args()

    if args.session and not args.docker_volume:
        fail("--session is only valid together with --docker-volume")
    if args.docker_volume and not args.session:
        fail("--docker-volume also needs --session NAME")
    if bool(args.gateway_url) != bool(args.alias):
        fail("--gateway-url and --alias must be given together")

    if args.pyrogram_file:
        sess = from_pyrogram_db(args.pyrogram_file)
    elif args.telethon_file:
        sess = from_telethon_db(args.telethon_file)
    elif args.pyrogram_string:
        sess = from_pyrogram_string(read_stdin("pyrogram string"))
    elif args.telethon_string:
        sess = from_telethon_string(read_stdin("telethon string"))
    else:
        sess = from_docker_volume(args.docker_volume, args.session)

    # ---- validate the session itself
    if sess["is_bot"]:
        fail("Telegram Desktop cannot log in as a bot")
    if sess["test_mode"]:
        fail("refusing a test-mode session; use a production session")
    dc_id = sess["dc_id"]
    if not isinstance(dc_id, int) or not 1 <= dc_id <= 5:
        fail("dc_id must be an integer in 1..5, got %r" % (dc_id,))
    auth_key = sess["auth_key"]
    if len(auth_key) != AUTH_KEY_LEN:
        fail("auth_key must be exactly %d bytes, got %d"
             % (AUTH_KEY_LEN, len(auth_key)))

    user_id = sess["user_id"]
    if user_id is None:
        if args.user_id is None:
            fail("%s sessions do not store the user id;"
                 " pass --user-id N" % sess["kind"])
        user_id = args.user_id
    elif args.user_id is not None and args.user_id != user_id:
        fail("--user-id %d does not match the user id stored in the session"
             " (%d)" % (args.user_id, user_id))
    if not isinstance(user_id, int) or user_id <= 0:
        fail("user_id must be a positive integer, got %r" % (user_id,))

    out = args.out or default_out_path()
    if os.path.lexists(out) and not args.force:
        fail("refusing to overwrite existing %s (use --force to override)"
             % out)

    # ---- AUTH_KEY_DUPLICATED guard: the key must have exactly one user.
    if args.gateway_url:
        pause_gateway(args.gateway_url, args.alias)
    elif not args.source_is_stopped:
        fail("an auth key must only be used by one client at a time: if"
             " Telegram sees two clients it reports AUTH_KEY_DUPLICATED and"
             " may revoke the key.  Either pass --source-is-stopped (you are"
             " sure the gateway/client that produced this key is no longer"
             " running), or --gateway-url URL --alias ALIAS (the session is"
             " paused through the gateway admin API first).  Nothing was"
             " written.")

    payload = {"dc_id": dc_id, "user_id": user_id,
               "auth_key": base64.b64encode(auth_key).decode("ascii"),
               "is_bot": False}
    atomic_write_private(out, (json.dumps(payload, indent=2) + "\n").encode())

    print("wrote %s for user %d on DC %d; start Aethergram (not logged in) to"
          " import it — the file is wiped after reading."
          % (out, user_id, dc_id))


if __name__ == "__main__":
    main()
