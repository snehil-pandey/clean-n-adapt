#!/usr/bin/env python3
"""Smoke tests for cna. Builds a fake Windows-style profile in a temp dir, points the
environment variables at it, and checks behaviour end to end. Works on Linux/macOS/Windows.

    python3 tests/smoke.py ./cna          (or path\\to\\cna.exe)
Exit code 0 = all checks passed.
"""
import json, os, shutil, subprocess, sys, tempfile, time

CNA = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./cna")
root = tempfile.mkdtemp(prefix="cna-smoke-")
user = os.path.join(root, "user")
LA, RA = os.path.join(user, "AppData", "Local"), os.path.join(user, "AppData", "Roaming")
PD, PF = os.path.join(root, "ProgramData"), os.path.join(root, "PF")
state = os.path.join(root, "state")
env = dict(os.environ, USERPROFILE=user, LOCALAPPDATA=LA, APPDATA=RA, TEMP=os.path.join(LA, "Temp"),
           PROGRAMDATA=PD, PROGRAMFILES=PF, SystemRoot=os.path.join(root, "Windows"), CNA_HOME=state)

def w(path, size=0, age_days=0, data=None):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data if data is not None else os.urandom(size))
    if age_days:
        t = time.time() - age_days * 86400
        os.utime(path, (t, t))

def run(*a):
    return subprocess.run([CNA, *a], env=env, capture_output=True, text=True)

def js(*a):
    p = run(*a, "--json")
    return json.loads(p.stdout)

results = []
def check(name, ok):
    print(("PASS  " if ok else "FAIL  ") + name)
    results.append(bool(ok))

try:
    # ---- fixture -------------------------------------------------------------
    w(LA + "/Temp/old.tmp", 5000, 10); w(LA + "/Temp/new.tmp", 3000, 0); w(LA + "/Temp/sub/deep.tmp", 2000, 5)
    cache = LA + "/Google/Chrome/User Data/Default/"
    w(cache + "Cache/Cache_Data/f_1", 100000); w(cache + "Bookmarks", 100); w(cache + "Login Data", 100)
    big = os.urandom(3 * 1024 * 1024)
    w(user + "/Downloads/movie.mkv", data=big); w(user + "/Documents/copy.mkv", data=big, age_days=20)
    w(user + "/Desktop/movie (1).mkv", data=big)
    tweaked = bytearray(big); tweaked[-1] ^= 1
    w(user + "/Documents/almost.mkv", data=bytes(tweaked))
    os.link(user + "/Downloads/movie.mkv", user + "/Documents/hardlink.mkv")
    w(LA + "/OldGameLauncher/data.db", 50000, 400); w(RA + "/Zoomlike/settings.json", 500, 200)
    w(LA + "/RecentThing/a.cfg", 500, 2); w(LA + "/NotepadStuff/a", 500, 500); w(LA + "/Microsoft/keep.txt", 5, 900)
    apps = os.path.join(root, "apps.txt")
    open(apps, "w").write("Notepad|Notepad Team|%s/PF/Notepad\nDeadApp|Foo|%s/PF/DeadApp\n" % (root, root))

    # ---- cleaning ------------------------------------------------------------
    r = js("scan", "--profile", "quick,browser")
    check("scan reports temp bytes (old files only)", r["total_bytes"] >= 7000 and r["total_bytes"] < 100000 + 8000 + 7000 + 1)
    run("clean", "--profile", "quick,browser")           # no --yes: must not delete
    check("clean without --yes deletes nothing", os.path.exists(LA + "/Temp/old.tmp"))
    run("clean", "--profile", "quick,browser", "--yes")
    check("clean --yes removes old temp + browser cache",
          not os.path.exists(LA + "/Temp/old.tmp") and not os.path.exists(cache + "Cache/Cache_Data/f_1"))
    check("recent file (<1 day) is kept", os.path.exists(LA + "/Temp/new.tmp"))
    check("bookmarks / logins are never touched",
          os.path.exists(cache + "Bookmarks") and os.path.exists(cache + "Login Data"))

    # ---- safety: hostile config ---------------------------------------------
    run("config", "init")
    with open(os.path.join(state, "cna.conf"), "a") as f:
        f.write("custom_rule = ${USERPROFILE}\ncustom_rule = ${USERPROFILE}/Documents\nprofiles = custom\nclean = true\n")
    run("auto", "run", "--force")
    check("protected custom rules are refused (Documents intact)", os.path.exists(user + "/Documents/almost.mkv"))

    # ---- duplicates ----------------------------------------------------------
    d = js("dupes", user, "--min-size", "1M")
    check("exactly one duplicate set", len(d["sets"]) == 1)
    paths = [os.path.basename(p) for p in d["sets"][0]["paths"]] if d["sets"] else []
    check("same-size-but-different file is not a duplicate", "almost.mkv" not in paths)
    check("hard link is not counted as a separate copy", len(paths) == 3 and not ("hardlink.mkv" in paths and "movie.mkv" in paths))
    o = js("dupes", "--of", user + "/Downloads/movie.mkv", user)
    check("--of finds other copies of one file", len(o["sets"]) == 1 and len(o["sets"][0]["paths"]) >= 3)

    # ---- quarantine round trip ----------------------------------------------
    js("dupes", user, "--min-size", "1M", "--quarantine", "--yes")
    q = js("quarantine", "list")
    check("extra copies moved to quarantine", len(q["entries"]) == 2)
    keep = [p for p in (user + "/Downloads/movie.mkv", user + "/Documents/copy.mkv", user + "/Desktop/movie (1).mkv") if os.path.exists(p)]
    check("one real copy survives", len(keep) >= 1)
    entry = q["entries"][0]
    run("quarantine", "restore", entry["id"])
    check("restore puts the file back", os.path.exists(entry["original"]))

    # ---- remnants ------------------------------------------------------------
    rm = js("remnants", "--apps-file", apps, "--min-score", "60")
    flagged = {os.path.basename(x["path"]) for x in rm["remnants"]}
    check("old unowned folders are flagged", {"OldGameLauncher", "Zoomlike"} <= flagged)
    check("recent / owned / system folders are not flagged", not ({"RecentThing", "NotepadStuff", "Microsoft"} & flagged))
    ap = js("remnants", "--app", "oldgame", "--apps-file", apps)
    check("--app finds leftovers by name", any("OldGameLauncher" in x["path"] for x in ap["paths"]))

    # ---- explain / knowledge base -------------------------------------------
    w(user + "/proj/package.json", data=b"{}"); w(user + "/proj/node_modules/x/i.js", 10)
    e = js("explain", user + "/proj/node_modules")
    check("explain: node_modules is regenerable", e["safety"] == "regenerable")
    js("explain", user + "/proj")
    kb = js("kb", "search", "node")
    check("knowledge base stores answers", kb["count"] >= 1)

    # ---- memory cap ----------------------------------------------------------
    p = run("mem", "--mem-cap", "8")
    check("--mem-cap below 16 MB is rejected", p.returncode == 64)
finally:
    shutil.rmtree(root, ignore_errors=True)

print("\n%d/%d checks passed" % (sum(results), len(results)))
sys.exit(0 if all(results) else 1)
