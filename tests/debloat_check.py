#!/usr/bin/env python3
"""Tests for `cna debloat` using the file-backed fake registry (Linux/macOS) - no real system is touched.

    python3 tests/debloat_check.py ./cna
On Windows the same logic runs against the real registry; use a throw-away profile/VM for that (see docs/TESTING.md).
"""
import json, os, shutil, subprocess, sys, tempfile

CNA = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./cna")
root = tempfile.mkdtemp(prefix="cna-debloat-")
state = os.path.join(root, "state"); os.makedirs(state)
REG, EXEC, PKGS = os.path.join(root, "reg.tsv"), os.path.join(root, "exec.log"), os.path.join(root, "pkgs.txt")
env = dict(os.environ, CNA_HOME=state, CNA_FAKE_REGISTRY=REG, CNA_FAKE_EXEC=EXEC, USERPROFILE=os.path.join(root, "u"),
           APPDATA=os.path.join(root, "u", "AppData", "Roaming"), PROGRAMDATA=os.path.join(root, "pd"), CNA_FAKE_ADMIN="1")
RUN = "Software\\Microsoft\\Windows\\CurrentVersion\\Run"
APPR = "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run"
SVC = "SYSTEM\\CurrentControlSet\\Services\\"

def seed(rows):
    with open(REG, "w") as f:
        esc = lambda x: x.replace("\\", "\\\\")           # the fake registry file is TSV-escaped
        for h, k, n, t, d in rows: f.write("\t".join([h, esc(k), esc(n), t, esc(d)]) + "\n")
def regdump():
    out = {}
    if os.path.exists(REG):
        for line in open(REG):
            c = line.rstrip("\n").split("\t")
            if len(c) >= 5:
                un = lambda x: x.replace("\\\\", "\\")
                out[(c[0], un(c[1]).lower(), un(c[2]).lower())] = (c[3], un(c[4]))
    return out
def get(h, k, n): return regdump().get((h, k.lower(), n.lower()))
def run(*a, admin="1"):
    e = dict(env, CNA_FAKE_ADMIN=admin)
    return subprocess.run([CNA, *a], env=e, capture_output=True, text=True)
def js(*a, **kw): return json.loads(run(*a, "--json", **kw).stdout)
def execlog(): return open(EXEC).read().splitlines() if os.path.exists(EXEC) else []
results = []
def check(name, ok): print(("PASS  " if ok else "FAIL  ") + name); results.append(bool(ok))

try:
    seed([
        ("HKCU", RUN, "AdobeAAMUpdater-1.0", "sz", "C:\\Adobe\\AAMUpdater.exe"),
        ("HKCU", RUN, "Spotify", "sz", "C:\\Spotify\\Spotify.exe --minimized"),
        ("HKCU", RUN, "OneDrive", "sz", "C:\\OneDrive\\OneDrive.exe /background"),
        ("HKCU", RUN, "MyCustomTool", "sz", "C:\\tools\\mytool.exe"),
        ("HKLM", RUN, "RtkAudUService", "sz", "C:\\Windows\\System32\\RtkAudUService64.exe"),
        ("HKLM", RUN, "SecurityHealth", "sz", "C:\\Windows\\System32\\SecurityHealthSystray.exe"),
        ("HKLM", SVC + "RetailDemo", "Start", "dword", "3"), ("HKLM", SVC + "Fax", "Start", "dword", "3"),
        ("HKLM", SVC + "DiagTrack", "Start", "dword", "2"), ("HKLM", SVC + "SysMain", "Start", "dword", "2"),
        ("HKLM", SVC + "WinDefend", "Start", "dword", "2"), ("HKLM", SVC + "wuauserv", "Start", "dword", "3"),
        ("HKLM", "SYSTEM\\CurrentControlSet\\Control\\Power", "HibernateEnabled", "dword", "1"),
        ("HKCU", "Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo", "Enabled", "dword", "1"),
    ])
    open(PKGS, "w").write("\n".join([
        "Microsoft.BingNews_4.55.62231.0_x64__8wekyb3d8bbwe", "Microsoft.WindowsStore_22401.1400.6.0_x64__8wekyb3d8bbwe",
        "king.com.CandyCrushSaga_1.2.3.4_x64__kgqvnymyfvs32", "Microsoft.XboxApp_48.0_x64__8wekyb3d8bbwe",
        "Microsoft.WindowsCalculator_11.2_x64__8wekyb3d8bbwe", "Microsoft.Windows.Photos_2024.1_x64__8wekyb3d8bbwe",
        "Evil'; calc_1.0_x64__x", "Microsoft.GetHelp_10.1_x64__8wekyb3d8bbwe"]))
    before = regdump()

    # ---- scan ------------------------------------------------------------------------------------------------
    s = js("debloat", "scan", "--packages-file", PKGS)
    ids = {i["id"]: i for i in s["items"]}
    check("scan lists 18 privacy tweaks", sum(1 for i in s["items"] if i["category"] == "privacy") == 18)
    check("scan finds startup entries incl. unrecognised", "startup:MyCustomTool" in ids and ids["startup:MyCustomTool"]["tier"] == 0)
    check("security/driver startup entries are protected", ids["startup:SecurityHealth"]["tier"] == -1 and ids["startup:RtkAudUService"]["tier"] == -1)
    check("only allow-listed services appear (no WinDefend/wuauserv)", "svc:WinDefend" not in ids and "svc:wuauserv" not in ids and "svc:DiagTrack" in ids)
    check("Store, Calculator, Photos never offered", not any(k in ids for k in ("app:Microsoft.WindowsStore", "app:Microsoft.WindowsCalculator", "app:Microsoft.Windows.Photos")))
    check("injection-style package name is dropped", not any("Evil" in i["detail"] for i in s["items"]))
    check("apps listed: BingNews, CandyCrush, Xbox, GetHelp", all(k in ids for k in ("app:Microsoft.BingNews", "app:king.com.CandyCrush", "app:Microsoft.XboxApp", "app:Microsoft.GetHelp")))
    check("scan changes nothing", regdump() == before and not execlog())

    # ---- dry run ---------------------------------------------------------------------------------------------
    d = js("debloat", "apply", "--preset", "balanced", "--packages-file", PKGS)
    check("apply without --yes is a dry run", d["dry_run"] and regdump() == before and not execlog())

    # ---- safe preset ---------------------------------------------------------------------------------------------
    a1 = js("debloat", "apply", "--preset", "safe", "--yes", "--packages-file", PKGS)
    check("safe preset applied without failures", a1["failed"] == 0 and a1["applied"] > 8)
    check("ads-id tweak written", get("HKCU", "Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo", "Enabled") == ("dword", "0"))
    check("safe service RetailDemo disabled", get("HKLM", SVC + "RetailDemo", "Start") == ("dword", "4"))
    check("balanced service DiagTrack untouched by 'safe'", get("HKLM", SVC + "DiagTrack", "Start") == ("dword", "2"))
    check("safe startup entry (AdobeAAMUpdater) disabled like Task Manager", (get("HKCU", APPR, "AdobeAAMUpdater-1.0") or ("", ""))[1].startswith("03"))
    check("balanced startup entry (Spotify) untouched by 'safe'", get("HKCU", APPR, "Spotify") is None)
    check("unrecognised + protected startup entries untouched", get("HKCU", APPR, "MyCustomTool") is None and get("HKCU", APPR, "SecurityHealth") is None)
    check("Run values themselves never edited", get("HKCU", RUN, "AdobeAAMUpdater-1.0") == ("sz", "C:\\Adobe\\AAMUpdater.exe"))
    check("no Store app removed by default", not any("Remove-AppxPackage" in c for c in execlog()))
    check("hibernation untouched by 'safe'", not any("powercfg" in c for c in execlog()))
    check("re-running is idempotent", js("debloat", "apply", "--preset", "safe", "--yes", "--packages-file", PKGS)["applied"] == 0)

    # ---- balanced / aggressive -------------------------------------------------------------------------------
    js("debloat", "apply", "--preset", "balanced", "--yes", "--packages-file", PKGS)
    check("balanced adds Spotify + DiagTrack", (get("HKCU", APPR, "Spotify") or ("", ""))[1].startswith("03") and get("HKLM", SVC + "DiagTrack", "Start") == ("dword", "4"))
    check("OneDrive (aggressive tier) not touched by balanced", get("HKCU", APPR, "OneDrive") is None)
    js("debloat", "apply", "--preset", "aggressive", "--yes", "--packages-file", PKGS)
    check("aggressive adds SysMain, OneDrive, hibernation off", get("HKLM", SVC + "SysMain", "Start") == ("dword", "4") and (get("HKCU", APPR, "OneDrive") or ("", ""))[1].startswith("03") and any("powercfg.exe /hibernate off" in c for c in execlog()))

    # ---- Store apps: explicit opt-in only -------------------------------------------------------------------
    open(EXEC, "w").close()
    js("debloat", "apply", "--preset", "safe", "--apps", "--yes", "--packages-file", PKGS)
    cmds = [c for c in execlog() if "Remove-AppxPackage" in c]
    check("--apps removes safe-tier apps only (BingNews, CandyCrush, GetHelp)", len(cmds) == 3 and not any("XboxApp" in c for c in cmds))
    check("allow-list holds: Store/Calculator/Photos never in a command", not any(x in " ".join(cmds) for x in ("WindowsStore", "Calculator", "Photos", "Evil")))
    check("command uses the full package name, no shell tricks", all(c.startswith("powershell.exe -NoProfile -NonInteractive") and c.count("'") == 2 for c in cmds))

    # ---- admin gating --------------------------------------------------------------------------------------------
    seed([("HKLM", SVC + "RetailDemo", "Start", "dword", "3"), ("HKCU", "Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo", "Enabled", "dword", "1")])
    jsave_before = regdump()
    na = js("debloat", "apply", "--preset", "safe", "--yes", admin="0")
    skipped = {r["id"]: r["reason"] for r in na["results"] if r["status"] == "skipped"}
    check("without admin, HKLM items are skipped (not failed)", skipped.get("svc:RetailDemo") == "needs administrator" and na["failed"] == 0)
    check("without admin, HKCU items still apply", get("HKCU", "Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo", "Enabled") == ("dword", "0"))
    check("without admin, HKLM untouched", get("HKLM", SVC + "RetailDemo", "Start") == ("dword", "3"))

    # ---- exact undo ------------------------------------------------------------------------------------------------
    seed([("HKCU", RUN, "AdobeAAMUpdater-1.0", "sz", "x.exe"), ("HKLM", SVC + "Fax", "Start", "dword", "3"),
          ("HKCU", "Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo", "Enabled", "dword", "1")])
    shutil.rmtree(state); os.makedirs(state); open(EXEC, "w").close()
    orig = regdump()
    js("debloat", "apply", "--preset", "safe", "--yes")
    check("state changed after apply", regdump() != orig)
    un = run("debloat", "undo", "--all")                     # preview
    check("undo without --yes is a preview", regdump() != orig and "Preview" in un.stdout)
    run("debloat", "undo", "--all", "--yes")
    after = regdump()
    # values that did not exist before must be gone again; values that existed must have their old data back
    check("undo restores previous values exactly", after == orig)
    check("journal is empty after full undo", js("debloat", "journal")["entries"] == [])
    seed([("HKLM", "SYSTEM\\CurrentControlSet\\Control\\Power", "HibernateEnabled", "dword", "1")]); open(EXEC, "w").close()
    js("debloat", "apply", "--preset", "aggressive", "--only", "disk", "--yes"); run("debloat", "undo", "--all", "--yes")
    lg = execlog()
    check("hibernate off then on", any("/hibernate off" in c for c in lg) and any("/hibernate on" in c for c in lg))
    check("unknown --id is reported, protected id refused", any(r["status"] == "skipped" for r in js("debloat", "apply", "--id", "svc:WinDefend", "--id", "startup:SecurityHealth", "--yes")["results"]))
finally:
    shutil.rmtree(root, ignore_errors=True)
print("\n%d/%d checks passed" % (sum(results), len(results)))
sys.exit(0 if all(results) else 1)
