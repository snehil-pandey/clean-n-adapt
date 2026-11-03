# Testing and verification

This page separates what was **verified** from what was **not**, so you know how much to trust each part.

## 1. Automated smoke test (Linux)

    tests/smoke.sh        # needs g++ and GNU coreutils; ~10 s; works only inside a temp directory

It builds the CLI, creates a simulated Windows profile (via `USERPROFILE`, `LOCALAPPDATA`, `APPDATA`, `TEMP`, ...) and
asserts 21 behaviours:

| Area | Checks |
|---|---|
| Caches | `scan` never deletes; `clean` without `--yes` never deletes; old temp file removed; fresh temp file kept (minimum age); browser cache removed; bookmarks and login data untouched |
| Duplicates | exactly one set found; the set has 3 files (a hard link and a same-size-different-content file are excluded); wasted bytes = 2 x size; `--of` finds the other copies; extras moved to quarantine; restore brings a file back |
| Leftovers | old unowned folder flagged; recently used folder not flagged; system folder not flagged |
| Identification | `node_modules` classified `regenerable`; project folder recognised |
| Safety / cap | broad custom rules (`${USERPROFILE}`, `Documents`) refused and Documents left intact; unknown profile -> exit 64; `--mem-cap` below 16 -> exit 64; default cap is 125 MB |

Result at the time of writing: **21 passed, 0 failed.**

### 1b. Portable smoke test (Python, also for Windows)

    python3 tests/smoke.py ./cna            # Linux / macOS
    python tests\smoke.py build\Release\cna.exe   # Windows (needs Python 3)

The same idea as `smoke.sh` (fake profile through environment variables, nothing real is touched) but written in
Python so it can run against the **real `cna.exe` on Windows**, which is the first thing worth doing there.
19 checks: caches (dry-run, minimum age, bookmarks untouched), hostile custom rules refused, duplicates (one set,
same-size-different file excluded, hard link not counted, `--of`), quarantine round trip, leftovers (flagged /
not flagged, `--app`), `explain` + knowledge base, `--mem-cap` validation. Result on Linux: **19 passed, 0 failed.**
Run on Windows: *not yet done* (the script avoids POSIX-only calls, but that is untested).

### 1c. Model, advisor and debloat suites (Linux, no installation needed)

    python3 tests/model_check.py ./cna        python3 tests/advisor_check.py ./cna        python3 tests/debloat_check.py ./cna

| Suite | What it does | Result |
|---|---|---|
| `model_check.py` | 30 hand-built, realistic folders (Chrome cache, Discord, `.ssh`, `Package Cache`, a Telegram `tdata`, ...) judged on the final **action** | **11 / 12** caches recognised as `clean`; **18 / 18** non-caches kept out of `clean` (0 dangerous). Known miss: a cache named just `Data` stays `review` |
| `cna model eval` | grouped 5-fold cross-validation on the built-in (synthetic) seed | 90.7% accuracy; raw dangerous errors 17 / 1,459 (1.2%), 0 pass the name gate |
| `advisor_check.py` | fake AppData -> `recommend` -> `--apply` -> restore -> feedback | **21 / 21**: caches found five levels deep, Credentials / tdata / profile never cleaned, nothing moves without `--apply --yes`, restore is learned, explicit feedback obeyed and forgettable |
| `debloat_check.py` | file-backed fake registry + recorded commands | **35 / 35**: dry-run changes nothing, presets nest, protected and unrecognised startup entries untouched, Store/Calculator/Photos never offered, injection-style package name dropped, apps need `--apps`, admin gating, **exact undo**, hibernate off/on |

The first run of `model_check.py` found a real defect (a `Credentials` folder and Telegram `tdata` were called regenerable because their hex-named
files look like cache blobs). It led to hard-negative training data and the non-learned safety gate described in MODEL.md.

## 2. Stress and correctness at scale (Linux)

Dataset: 300,000 files (1,500 directories x 200 files, 50-3,000 bytes each, ~1.2 GB on disk, about half drawn from a
pool of 2,000 payloads so many true duplicates exist).

| Measurement | Result |
|---|---|
| `dupes --min-size 1` over all 300,000 files, 125 MB cap | peak resident memory **31.3 MB**, 8.2 s |
| Same, `--mem-cap 20` | stops with exit code 3 and a clear message instead of crashing (peak 15.7 MB) |
| Correctness | identical to an independent Python SHA-256 reference: **2,000 sets, 228,843,835 bytes wasted** |

Reproduce with `python3 tests/stress.py ./cna [N_FILES]` (default 300,000; needs about 1.2 GB of temp disk). It builds the
dataset, computes the reference with `hashlib`, runs `cna dupes --json`, compares set count and wasted bytes, and reports
peak RSS. A 20,000-file run gives identical results with 22 MB peak.

These numbers are for the Linux build in a container; absolute speed and memory on Windows will differ, but the
algorithms and data structures are the same.

## 3. Windows build

* The CLI and the widget **cross-compile warning-free** (`-Wall -Wextra`) with MinGW-w64 (GCC 13, POSIX threads),
  static, C++17. Sizes at the time of writing: `cna.exe` ~1.2 MB, `cna-widget.exe` ~0.3 MB.
* The MSVC path in `CMakeLists.txt` (`/MT`, `/O1`, `/utf-8`) was written but **not** built.

## 4. Wine session (Wine 9.0 under a virtual X display)

The Windows binaries were run under Wine with scripted mouse clicks and screenshots:

* CLI: `dupes` found 3 distinct files out of 4 paths (one was a hard link), using the Windows file-ID code path.
* Widget, light and dark themes: layout, text, progress bar, profile chips, tiles and toggle render correctly.
* Widget flow: **Scan** -> action button becomes `Clean 145.0 MB` -> **Confirm** -> files actually deleted (Temp folder
  3 files -> 0) -> **Duplicates** -> `Quarantine extras` -> the extra copy moved out of Downloads.
* A bug found this way and fixed: the startup `auto status` query used to put the whole UI in a "busy" state.

Wine is a useful smoke test, not a substitute for Windows: its GDI+, DWM and Task Scheduler implementations are partial,
and the system font set differs (icon glyphs rendered as empty boxes there).

### 4b. Installer session (NSIS installer under Wine 9.0, fresh prefix, 32-bit support added)

Silent mode (`/S`) unless noted. Everything below passed:

* **Install:** files, 4 Start Menu shortcuts, "Apps & features" registry entry (name, version, location, uninstall string).
* **Installed CLI:** `cna version` works; `config init` wrote to `%LOCALAPPDATA%\cna` and **no `.state` folder** appeared next to the exe.
* **Widget lifecycle:** `cna-widget.exe` starts; `cna-widget.exe --quit` stops it (process count 1 -> 0).
* **Upgrade over a running widget:** installer exits it, replaces the files, exit code 0.
* **Uninstall (silent):** program folder, shortcuts, Apps entry and the `Run` value all removed; a fake quarantined file and
  `cna.conf` in `%LOCALAPPDATA%\cna` were **kept**.
* **Wizard (interactive):** welcome, components and folder pages rendered and navigated with the keyboard; branding and icon appear.

Not exercised: the optional *Desktop shortcut* / *Start with Windows* components, the uninstall question's *Yes* path,
the finish page, installing to a path with spaces or non-ASCII characters, and SmartScreen behaviour.

### 4c. Model, advisor and debloat under Wine (real Windows registry code)

* `recommend` on a fake profile in the Wine prefix: caches five levels deep suggested, `Microsoft\Credentials` and Telegram `tdata` not cleaned, Chrome profile data not cleaned.
* `debloat apply --preset balanced --only privacy,startup` against **Wine's real registry**, then verified with an independent `reg query`:
  the advertising DWORD flipped to 0, `StartupApproved\Run` binaries written for the recognised entries, `Run` values intact, protected and unrecognised entries untouched.
  `debloat undo --all --yes` restored all **34** journal rows: the original value is back and values that did not exist are gone again.
* Widget: **Smart advice** tile -> "50.0 MB, 4 folders suggested" -> `Quarantine 50.0 MB` -> **Confirm** -> the cache folders really moved to quarantine (Credentials and Bookmarks untouched);
  **Debloat** tile -> "10 safe" -> `Apply 10 safe tweaks`.
* `cna quarantine restore` of an advisor-quarantined folder printed "Noted: the model will treat folders like this one more carefully", and `model predict` then reported `caution` ("your earlier decision").
* Test-harness note: on a bare virtual display the very first click after launching the widget can be swallowed; moving the mouse first avoids it. This is not seen with a real window manager.

## 5. Not verified

| Area | Why it matters |
|---|---|
| Real Windows 10/11 run of either exe | Everything below depends on it |
| Model accuracy on real machines | The seed is synthetic and written by the author; the 30-folder check is independent but still small and by the same author |
| Debloat: PowerShell Store-app removal, service changes, `powercfg`, Windows 11 protected taskbar values, `StartupApproved` for HKLM Run entries | Only the registry read/write/undo path and the command strings were exercised (Wine / recorded commands) |
| Debloat after a Windows feature update | Windows can reset settings; not observed |
| Job-Object memory limit (`ProcessMemoryLimit`) | Kernel enforcement of the 125 MB cap; the internal budget check is tested, the OS layer on Windows is not |
| Registry app inventory and `--app` registry search | Only compiled; the Linux/Wine runs used `--apps-file` |
| Task Scheduler integration (`auto install/remove/status`) | Runs `schtasks.exe`; behaviour under policies/non-admin accounts untested |
| Junction and OneDrive-placeholder detection | Attribute checks are written to documented flags but not exercised |
| Tray menu, notifications, Explorer-restart recovery, dark tray menu (uxtheme ordinals) | Requires a real shell |
| Rounded corners, border colour, shadow, per-monitor DPI changes, live theme switching | Requires DWM |
| Non-ASCII user names / long paths | UTF-8 handling is implemented; not exercised |
| The optional local-LLM path | Not implemented (stub only) |
| MSVC build | Not built |
| Installer on real Windows (UAC-free per-user install, Start Menu, Apps list, upgrade, uninstall prompt) | Only run under Wine |
| GitHub Actions workflow | Written, never run |

## 6. Windows verification checklist (about 15 minutes)

Do these in order on a test machine or with a throw-away user profile.

1. `cna version`, `cna mem` - prints version and `cap: 125.0 MB`.
2. `cna scan --profile full -v` - totals look plausible; `windows` rules listed as "needs admin" when not elevated.
3. `cna clean --profile quick` (no `--yes`) - nothing is deleted; then with `--yes` - Temp shrinks; files in use are reported, not errors.
4. Create `D:\t\a.bin` (5 MB), copy it twice, make a hard link; `cna dupes D:\t --min-size 1M` - 3 distinct files listed, hard link not counted.
5. `cna dupes D:\t --quarantine --yes` then `cna quarantine list` / `restore` - file returns.
6. `cna remnants` - compare the list against apps you know you uninstalled; `cna explain` a few entries.
7. `cna remnants --app <an app you removed>` - registry section lists real keys (read-only).
8. Memory cap: run a large `cna dupes C:\ --min-size 1` and watch Task Manager - the process stays below ~125 MB; with `--mem-cap 30` it stops with exit code 3.
9. `cna auto install --every-hours 1`, check **Task Scheduler** for `cna-auto`; `cna auto status`; `cna auto remove`.
10. Widget: start it, check light and dark mode, drag, pin, close/reopen from the tray, tray right-click menu, **Quick clean now**
    notification, **Start with Windows**, move it to a second monitor / change scaling.
11. Installer: run `Clean-n-Adapt-Setup-<ver>.exe` normally (check the pages), tick both optional components, finish and launch;
    confirm *Settings > Apps* lists it; install the same file again over it (upgrade); then uninstall, answer **No** to the data question
    and confirm `%LOCALAPPDATA%\cna` is still there.
12. Plug a junction into a cleaned folder (`mklink /J`) and confirm the cleaner does not enter it.
13. `cna recommend` - read the list. Do the "clean" rows look like caches you recognise? Anything surprising? `cna recommend --apply --yes`, run your apps, then `cna quarantine restore` anything you miss.
14. `cna model feedback <folder> --label keep` on something the advisor offered; `cna recommend` again - it must not be offered. `cna model status` shows your decisions.
15. **In a VM or on a spare profile first:** `cna debloat scan`, `cna debloat apply --preset safe --yes`, sign out and in, check Start/Settings/startup; then `cna debloat undo --all --yes` and confirm everything is back (Settings > Apps > Startup).
16. As administrator: `cna debloat apply --preset balanced --only services --yes`, restart, `services.msc` shows the services Disabled; undo and restart again.
17. `cna debloat apply --apps --preset safe --yes` on the spare profile: the listed apps disappear from Start; the Store, Calculator and Photos are still there.

Please note any deviation; every item above is a place where Windows could behave differently from the tests here.

## 7. Adding tests

`tests/smoke.sh` is a plain bash script (`tests/smoke.py` follows the same pattern with `w(path, size, age)` and `check(name, cond)`): add a block that creates a fixture with `mk <file> <bytes> [age_days]` and
asserts with `check "description" '<shell condition>'`. The environment variables at the top redirect every location
the CLI uses, so tests never touch real user data.
