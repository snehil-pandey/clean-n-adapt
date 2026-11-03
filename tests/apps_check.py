"""Safety regression checks for app inventory, official removal, and classified leftovers."""
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile


exe = pathlib.Path(sys.argv[1]).resolve()
root = pathlib.Path(tempfile.mkdtemp(prefix="cna-apps-"))
try:
    user = root / "user"
    local = user / "AppData" / "Local"
    roaming = user / "AppData" / "Roaming"
    for path in (local, roaming, root / "ProgramData", root / "Program Files", root / "Temp"):
        path.mkdir(parents=True, exist_ok=True)
    (local / "oldapp-data" / "Credentials").mkdir(parents=True)
    (local / "oldapp-data" / "Credentials" / "token.bin").write_bytes(b"secret")
    (local / "oldapp-cache" / "Cache").mkdir(parents=True)
    (local / "oldapp-cache" / "Cache" / "cache.bin").write_bytes(b"x" * 4096)
    apps = root / "apps.txt"
    apps.write_text("StillInstalled|Example Corp|" + str(root / "Program Files" / "StillInstalled") + "\n", encoding="utf-8")
    env = os.environ.copy()
    env.update(USERPROFILE=str(user), LOCALAPPDATA=str(local), APPDATA=str(roaming), PROGRAMDATA=str(root / "ProgramData"),
               PROGRAMFILES=str(root / "Program Files"), TEMP=str(root / "Temp"), CNA_HOME=str(root / "state"))

    def run(*args, expect=0):
        cp = subprocess.run([str(exe), *args], env=env, text=True, capture_output=True)
        if cp.returncode != expect:
            raise AssertionError(f"{args}: expected {expect}, got {cp.returncode}: {cp.stdout} {cp.stderr}")
        return cp.stdout

    listed = json.loads(run("apps", "list", "--apps-file", str(apps), "--json"))
    assert listed["apps"][0]["name"] == "StillInstalled"
    assert listed["apps"][0]["official_uninstaller"] is False
    run("apps", "remove", "--app", "StillInstalled", "--apps-file", str(apps), expect=1)
    audit = json.loads(run("apps", "leftovers", "--app", "oldapp", "--apps-file", str(apps), "--json"))
    rows = {pathlib.Path(item["path"]).name: item for item in audit["paths"]}
    assert rows["oldapp-data"]["action"] == "keep", rows
    assert "sensitive" in rows["oldapp-data"]["category"], rows
    assert rows["oldapp-cache"]["action"] != "keep", rows
    print("PASS: app removal guard and classified leftovers")
finally:
    shutil.rmtree(root, ignore_errors=True)
