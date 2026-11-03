#!/usr/bin/env python3
"""End-to-end test of `cna recommend` + learning: fake AppData -> recommendations -> apply (quarantine) ->
restore one (the model must learn from it) -> explicit feedback changes the verdict.

    python3 tests/advisor_check.py ./cna
"""
import json, os, shutil, subprocess, sys, tempfile, time

CNA = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./cna")
root = tempfile.mkdtemp(prefix="cna-adv-")
user = os.path.join(root, "user")
LA, RA, PD = os.path.join(user, "AppData", "Local"), os.path.join(user, "AppData", "Roaming"), os.path.join(root, "ProgramData")
env = dict(os.environ, USERPROFILE=user, LOCALAPPDATA=LA, APPDATA=RA, TEMP=os.path.join(LA, "Temp"), PROGRAMDATA=PD,
           PROGRAMFILES=os.path.join(root, "PF"), SystemRoot=os.path.join(root, "Windows"), CNA_HOME=os.path.join(root, "state"))
MB = 1 << 20
def mk(path, size, age=2):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f: f.truncate(size)
    t = time.time() - age * 86400; os.utime(path, (t, t))
def run(*a): return subprocess.run([CNA, *a], env=env, capture_output=True, text=True)
def js(*a): return json.loads(run(*a, "--json").stdout)
results = []
def check(name, ok): print(("PASS  " if ok else "FAIL  ") + name); results.append(bool(ok))

try:
    # --- fake AppData -----------------------------------------------------------------------------
    for i in range(6): mk(f"{LA}/Google/Chrome/User Data/Default/Cache/Cache_Data/f_{i:06x}", 3 * MB)
    mk(f"{LA}/Google/Chrome/User Data/Default/Bookmarks", 20000); mk(f"{LA}/Google/Chrome/User Data/Default/History", 6 * MB, 1)
    mk(f"{LA}/Google/Chrome/User Data/Default/Preferences", 40000, 1)
    for i in range(5): mk(f"{RA}/discord/Cache/Cache_Data/f_{i:06x}", 2 * MB)
    for i in range(5): mk(f"{LA}/Contoso/App/WebViewCache2/{i:06x}", 2 * MB, 4)         # odd name, still a cache
    for i in range(4): mk(f"{LA}/SomeVendor/Logs/app-2024-05-{i+1:02d}.log", 3 * MB, 30)
    for i in range(3): mk(f"{RA}/Microsoft/Credentials/{i:08X}", 8 * MB, 100)                  # blobs, but credentials!
    for i in range(3): mk(f"{RA}/Telegram Desktop/tdata/D877F783D5D3EF8C/map{i}", 3 * MB, 1)   # blobs, but chat data
    for n in ("settings.json", "profile.yaml", "user.cfg"): mk(f"{RA}/Quillsoft/{n}", 6 * MB, 20)
    mk(f"{PD}/Package Cache/{{aaaa}}v1/setup.exe", 8 * MB, 500)

    r = js("recommend", "--min-size", "1M")
    by = {os.path.basename(i["path"]): i for i in r["items"]}
    check("Chrome Cache (5 levels deep) is suggested",  any(i["path"].replace("\\", "/").endswith("User Data/Default/Cache") and i["action"] == "clean" for i in r["items"]))
    check("discord Cache is a suggested clean-up",    any(i["path"].endswith("discord/Cache") and i["action"] == "clean" for i in r["items"]))
    check("odd-named WebViewCache2 recognised",       by.get("WebViewCache2", {}).get("action") == "clean")
    check("parents are not double counted",          sum(i["bytes"] for i in r["items"]) <= 2 * r["clean_bytes"] + r["review_bytes"] and r["review_bytes"] < 80 * MB)
    check("Logs folder is a suggested clean-up",      by.get("Logs", {}).get("action") == "clean")
    check("Credentials is never cleaned",             not any(i["path"].endswith("Credentials") and i["action"] == "clean" for i in r["items"]))
    check("Telegram tdata is never cleaned",          not any("tdata" in i["path"] and i["action"] == "clean" for i in r["items"]))
    check("Quillsoft settings app only 'review'",     by.get("Quillsoft", {}).get("action") == "review")
    check("Package Cache is not suggested",           not any("Package Cache" in i["path"] for i in r["items"] if i["action"] == "clean"))
    check("Chrome profile (Bookmarks) not cleaned",   not any(i["path"].endswith("Default") and i["action"] == "clean" for i in r["items"]))
    check("items carry reasons",                      all(("because" in i) for i in r["items"]))
    check("clean total is plausible (> 30 MB)",       r["clean_bytes"] > 30 * MB)

    run("recommend", "--min-size", "1M")                                       # no --apply: nothing may move
    check("no --apply => nothing moved",              os.path.exists(f"{LA}/Google/Chrome/User Data/Default/Cache"))
    ap = js("recommend", "--min-size", "1M", "--apply", "--yes")
    check("--apply quarantines clean items",          ap["quarantined"] >= 3 and not os.path.exists(f"{LA}/Google/Chrome/User Data/Default/Cache"))
    check("credentials / tdata / profile untouched",  os.path.exists(f"{RA}/Microsoft/Credentials") and os.path.exists(f"{RA}/Telegram Desktop/tdata") and os.path.exists(f"{LA}/Google/Chrome/User Data/Default/Bookmarks"))

    # --- learning from a restore -----------------------------------------------------------------
    q = js("quarantine", "list")
    ent = next(e for e in q["entries"] if e["original"].endswith("WebViewCache2"))
    before = js("model", "status")["feedback"]
    out = run("quarantine", "restore", ent["id"]).stdout
    check("restore brings the folder back",           os.path.exists(ent["original"]))
    check("restore is recorded as feedback",          js("model", "status")["feedback"] == before + 1 and "Noted" in out)
    check("restored folder is no longer 'clean'",     js("model", "predict", ent["original"])["action"] != "clean")

    # --- explicit feedback ----------------------------------------------------------------------
    quill = f"{RA}/Quillsoft"
    run("model", "feedback", quill, "--label", "regenerable")
    check("explicit feedback is obeyed for that folder", js("model", "predict", quill)["action"] == "clean")
    run("model", "forget", quill)
    check("forgetting removes the decision",          js("model", "predict", quill)["action"] == "review")
    check("model reset needs --yes",                  "--yes" in run("model", "reset").stdout)
finally:
    shutil.rmtree(root, ignore_errors=True)
print("\n%d/%d checks passed" % (sum(results), len(results)))
sys.exit(0 if all(results) else 1)
