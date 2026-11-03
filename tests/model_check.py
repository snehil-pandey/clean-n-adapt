#!/usr/bin/env python3
"""Independent check of the folder model: builds realistic folder structures by hand (NOT from the
model's own seed templates), asks `cna model predict` about each one, and compares with what a
careful human would say.  Judged on the final ACTION the app would take:
R = must be "clean", N = must NOT be "clean" (safety check).

    python3 tests/model_check.py ./cna
"""
import json, os, shutil, subprocess, sys, tempfile, time

CNA = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./cna")
root = tempfile.mkdtemp(prefix="cna-model-")
user = os.path.join(root, "user")
LA, RA, PD = os.path.join(user, "AppData", "Local"), os.path.join(user, "AppData", "Roaming"), os.path.join(root, "ProgramData")
env = dict(os.environ, USERPROFILE=user, LOCALAPPDATA=LA, APPDATA=RA, TEMP=os.path.join(LA, "Temp"), PROGRAMDATA=PD,
           PROGRAMFILES=os.path.join(root, "PF"), SystemRoot=os.path.join(root, "Windows"), CNA_HOME=os.path.join(root, "state"))

def mk(path, size=0, age=0):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.truncate(size)
    if age:
        t = time.time() - age * 86400
        os.utime(path, (t, t))

def hexn(i): return "%06x" % (i * 7919 % 0xffffff)

MB = 1 << 20
cases = []  # (label, expectation, base, relative path, [(file, size, age)])
def case(label, exp, base, rel, files): cases.append((label, exp, base, rel, files))

case("Chrome Cache",        "R", LA, "Google/Chrome/User Data/Default/Cache", [("Cache_Data/f_" + hexn(i), 3 * MB, 2) for i in range(12)] + [("Cache_Data/data_%d" % i, 4 * MB, 2) for i in range(4)] + [("Cache_Data/index", 262144, 2)])
case("Edge Code Cache",     "R", LA, "Microsoft/Edge/User Data/Default/Code Cache", [("js/%s_0" % hexn(i), 90000, 5) for i in range(15)] + [("wasm/index", 1000, 5)])
case("Chrome GPUCache",     "R", LA, "Google/Chrome/User Data/Default/GPUCache", [("data_%d" % i, 270000, 1) for i in range(4)] + [("index", 256, 1)])
case("Discord Cache",       "R", RA, "discord/Cache", [("Cache_Data/f_" + hexn(i), 2 * MB, 3) for i in range(8)])
case("Windows-style Temp",  "R", LA, "Temp", [("~DF%04X.tmp" % i, 40000, 20) for i in range(8)] + [("tmp%d.tmp" % i, 5000, 40) for i in range(6)] + [("setup_log.txt", 9000, 30)])
case("App Logs",            "R", LA, "SomeVendor/App/Logs", [("app-2024-05-%02d.log" % i, 600000, 40) for i in range(1, 20)])
case("CrashDumps",          "R", LA, "CrashDumps", [("app.exe.%d.dmp" % (1000 + i), 25 * MB, 90) for i in range(4)])
case("Crashpad reports",    "R", LA, "Slack/Crashpad", [("reports/%s.dmp" % hexn(i), 800000, 60) for i in range(5)] + [("settings.dat", 100, 60)])
case("NVIDIA DXCache",      "R", LA, "NVIDIA/DXCache", [("%s_0.toc" % hexn(i), 120000, 10) for i in range(10)] + [("%s_0.bin" % hexn(i), 800000, 10) for i in range(10)])
case("npm-cache",           "R", LA, "npm-cache", [("_cacache/content-v2/sha512/%s" % hexn(i), 200000, 30) for i in range(10)] + [("_logs/2024-01-01.log", 4000, 30)])
case("Spotify Data cache",  "R", LA, "Spotify/Data", [("%s.file" % hexn(i), 3 * MB, 5) for i in range(14)])
case("WebView cache (odd name)", "R", LA, "Contoso/WebViewCache2", [("%s" % hexn(i), 150000, 4) for i in range(12)])
case("Chrome Local Storage","N", LA, "Google/Chrome/User Data/Default/Local Storage", [("leveldb/000003.log", 90000, 1), ("leveldb/CURRENT", 16, 1), ("leveldb/LOCK", 0, 1), ("leveldb/MANIFEST-000001", 300, 1), ("leveldb/000005.ldb", 400000, 20)])
case("Chrome IndexedDB",    "N", LA, "Google/Chrome/User Data/Default/IndexedDB", [("https_mail.example.com_0.indexeddb.leveldb/%06d.ldb" % i, 2 * MB, 9) for i in range(5)] + [("https_mail.example.com_0.indexeddb.leveldb/LOCK", 0, 9)])
case("Chrome profile Default","N", LA, "Google/Chrome/User Data/Default", [("Bookmarks", 20000, 3), ("Preferences", 40000, 1), ("History", 3 * MB, 1), ("Cookies", 400000, 1), ("Login Data", 100000, 2), ("Favicons", 300000, 3)])
case("Notion app data",     "N", RA, "Notion", [("Partitions/notion/Local Storage/leveldb/000003.log", 50000, 1), ("Preferences", 800, 1), ("IndexedDB/x.leveldb/000004.ldb", MB, 1), ("logs/main.log", 20000, 1)])
case("Game saves",          "N", user, "Documents/My Games/Skyrim/Saves", [("save%d.ess" % i, 6 * MB, 100 + i) for i in range(8)])
case("Photos",              "N", user, "Pictures/Holiday 2024", [("IMG_%04d.jpg" % i, 4 * MB, 200) for i in range(30)] + [("clip.mp4", 90 * MB, 200)])
case("Obsidian vault",      "N", user, "Notes/Vault", [("Daily/2024-05-%02d.md" % i, 3000, 5) for i in range(1, 25)] + [(".obsidian/app.json", 200, 5)])
case("SSH keys",            "N", user, ".ssh", [("id_ed25519", 400, 300), ("id_ed25519.pub", 100, 300), ("known_hosts", 2200, 20), ("config", 300, 100)])
case("AWS credentials",     "N", user, ".aws", [("credentials", 120, 200), ("config", 80, 200)])
case("Package Cache",       "N", PD, "Package Cache", [("{%08x-aaaa-bbbb-cccc-0123456789ab}v14.38.33135/packages/vcRuntime/vc_runtimeMinimum_x64.msi" % (i + 1), 190000, 500) for i in range(4)] + [("{%08x-aaaa-bbbb-cccc-0123456789ab}v14.38.33135/VC_redist.x64.exe" % (i + 1), 5 * MB, 500) for i in range(4)])
case("Windows Credentials", "N", RA, "Microsoft/Credentials", [("%s" % hexn(i).upper(), 11000, 120) for i in range(4)])
case("DPAPI Protect",       "N", RA, "Microsoft/Protect", [("S-1-5-21-1111-2222-3333-1001/%08x-1111" % i, 468, 400) for i in range(3)])
case("OneDrive sync root",  "N", user, "OneDrive", [("Documents/cv.docx", 60000, 10), ("Pictures/a.jpg", 3 * MB, 10), ("Desktop/notes.txt", 400, 10)])
case("Git repo metadata",   "N", user, "code/myapp/.git", [("HEAD", 21, 3), ("config", 300, 3), ("index", 20000, 1)] + [("objects/%02x/%s" % (i, hexn(i)), 3000, 20) for i in range(12)])
case("Wallet",              "N", RA, "Exodus/exodus.wallet", [("seed.seco", 900, 40), ("passphrase.json", 300, 40), ("info.seco", 400, 40)])
case("Installed program dir","N", os.path.join(root, "PF"), "CoolEditor", [("CoolEditor.exe", 30 * MB, 60), ("unins000.exe", MB, 60), ("lib/core.dll", 8 * MB, 60), ("lib/ui.dll", 5 * MB, 60), ("config.ini", 500, 60)])
case("Telegram tdata",      "N", RA, "Telegram Desktop/tdata", [("D877F783D5D3EF8C/map1", 3000, 1), ("key_datas", 400, 20), ("settingss", 200, 1)])
case("Unknown settings-only app", "N", RA, "Quillsoft", [("settings.json", 2000, 20), ("profile.yaml", 700, 20), ("user.cfg", 300, 20)])

res = {"R_ok": 0, "R": 0, "N_ok": 0, "N": 0, "dangerous": 0}
rows = []
try:
    for label, exp, base, rel, files in cases:
        d = os.path.join(base, *rel.split("/"))
        for f, size, age in files:
            mk(os.path.join(d, *f.split("/")), size, age)
        p = subprocess.run([CNA, "model", "predict", d, "--json"], env=env, capture_output=True, text=True)
        out = json.loads(p.stdout)
        pred, conf = out["action"], out["confidence"]
        ok = (pred == "clean") if exp == "R" else (pred != "clean")
        res[exp] += 1
        res[exp + "_ok"] += int(ok)
        if exp == "N" and pred == "clean": res["dangerous"] += 1
        rows.append((("ok  " if ok else "MISS"), exp, label, pred, conf, out["because"][:2]))
finally:
    shutil.rmtree(root, ignore_errors=True)

for r in rows:
    print("%s  %s  %-28s -> %-12s %3.0f%%   %s" % (r[0], r[1], r[2], r[3], r[4] * 100, "; ".join(r[5])))
print("\ncaches recognised as regenerable : %d / %d" % (res["R_ok"], res["R"]))
print("non-caches kept out of 'clean'   : %d / %d   (dangerous: %d)" % (res["N_ok"], res["N"], res["dangerous"]))
sys.exit(0 if res["dangerous"] == 0 else 1)
