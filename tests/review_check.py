"""Regression checks for conservative remnants and explicit review selection.

Run with: python tests/review_check.py path\\to\\cna.exe
All data stays below one temporary directory.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time


if len(sys.argv) != 2:
    raise SystemExit("usage: review_check.py CNA_EXE")

CNA = os.path.abspath(sys.argv[1])
ROOT = tempfile.mkdtemp(prefix="cna-review-")
LOCAL = os.path.join(ROOT, "Local")
ROAMING = os.path.join(ROOT, "Roaming")
PROGRAMDATA = os.path.join(ROOT, "ProgramData")
PROGRAMFILES = os.path.join(ROOT, "ProgramFiles")
PROGRAMFILES86 = os.path.join(ROOT, "ProgramFilesX86")
TEMP = os.path.join(LOCAL, "Temp")
STATE = os.path.join(ROOT, "state")

ENV = os.environ.copy()
ENV.update({
    "LOCALAPPDATA": LOCAL,
    "APPDATA": ROAMING,
    "PROGRAMDATA": PROGRAMDATA,
    "PROGRAMFILES": PROGRAMFILES,
    "PROGRAMFILES(X86)": PROGRAMFILES86,
    "TEMP": TEMP,
    "USERPROFILE": os.path.join(ROOT, "User"),
})


def run(*args):
    p = subprocess.run([CNA, *args, "--state", STATE], env=ENV, text=True,
                       capture_output=True, check=False)
    if p.returncode not in (0, 2):
        raise AssertionError("command failed: %s\n%s" % (args, p.stderr))
    return json.loads(p.stdout)


def write(path, size=0, old=False):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.truncate(size)
    if old:
        then = time.time() - 240 * 86400
        os.utime(path, (then, then))


try:
    old_cache = os.path.join(LOCAL, "OldToolCache")
    write(os.path.join(old_cache, "cache.bin"), 1024 * 1024, old=True)
    os.utime(old_cache, (time.time() - 240 * 86400,) * 2)
    os.makedirs(os.path.join(LOCAL, "EmptyFolder"), exist_ok=True)
    os.makedirs(os.path.join(PROGRAMDATA, "SoftwareDistribution"), exist_ok=True)
    apps = os.path.join(ROOT, "apps.txt")
    with open(apps, "w", encoding="utf-8") as f:
        f.write("Known App|Vendor|%s\n" % os.path.join(PROGRAMFILES, "Known"))

    rem = run("remnants", "--apps-file", apps, "--min-score", "0", "--json")
    names = {os.path.basename(row["path"]) for row in rem["remnants"]}
    assert "OldToolCache" in names, names
    assert "EmptyFolder" not in names, names
    assert "SoftwareDistribution" not in names, names
    assert all("scan_complete" in row for row in rem["remnants"]), rem

    run("remnants", "--apps-file", apps, "--min-score", "0", "--select", old_cache,
        "--quarantine", "--yes", "--json")
    assert not os.path.exists(old_cache), "selected remnant was not quarantined"

    dupes = os.path.join(ROOT, "dupes")
    a, b, c = (os.path.join(dupes, n) for n in ("a.bin", "b.bin", "c.bin"))
    write(a, 1024 * 1024)
    shutil.copyfile(a, b)
    shutil.copyfile(a, c)
    result = run("dupes", dupes, "--min-size", "1M", "--select", b, "--quarantine", "--yes", "--json")
    assert result["quarantined_files"] == 1, result
    assert os.path.exists(a) and not os.path.exists(b) and os.path.exists(c), "duplicate selection moved the wrong files"

    old_temp = os.path.join(TEMP, "selected.tmp")
    write(old_temp, 1024 * 1024, old=True)
    preview = run("clean", "--profile", "quick", "--rule", "user-temp", "--json")
    assert len(preview["rules"]) == 1 and preview["rules"][0]["rule"] == "user-temp", preview
    assert preview["rules"][0]["bytes"] >= 1024 * 1024, preview
    run("clean", "--profile", "quick", "--rule", "user-temp", "--yes", "--json")
    assert not os.path.exists(old_temp), "rule-scoped cleanup did not remove the selected rule target"

    held = run("quarantine", "list", "--json")["entries"]
    restore_ids = [entry["id"] for entry in held if entry["original"] in (b, old_cache)]
    restored = run("quarantine", "restore", "--id", restore_ids[0], "--id", restore_ids[1], "--json")
    assert restored["restored"] == 2 and restored["failed"] == 0, restored
    assert os.path.exists(b) and os.path.exists(old_cache), "multi-restore did not recover the selected entries"

    print("PASS: conservative remnants and selective quarantine")
finally:
    shutil.rmtree(ROOT, ignore_errors=True)
