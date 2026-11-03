# Command-line reference

    cna <command> [arguments] [options]

## Global options

| Option | Meaning |
|---|---|
| `--json` | Machine-readable output on stdout (warnings still go to stderr). |
| `--yes`, `-y` | Allow changes. Without it, every command that could delete or move only previews. |
| `--quiet`, `-q` | Suppress the text report of `scan`/`clean`/`auto run` and the memory-cap warning (used by the scheduled task). Errors are still printed. |
| `--verbose`, `-v` | Show extra detail (e.g. every folder a rule matched). |
| `--mem-cap MB` | Memory cap, default **125**, minimum 16. |
| `--state DIR` | Use this state directory (also `CNA_HOME`). |
| `--exclude PATH` | Never touch this path or anything under it. Repeatable. |
| `-h`, `--help` | Help. |

`cna help` and `cna version` need no other arguments. `cna mem` prints the cap and current usage.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Success |
| 1 | Error (bad path, unreadable config, nothing to compare against, failed restore, ...) |
| 2 | Partial: the command worked but some items could not be deleted/moved (in use, protected, cross-volume failure) |
| 3 | Stopped by the memory cap (message says how to narrow the scan) |
| 64 | Usage error (unknown command/profile, bad option value) |

## `scan` / `clean`

    cna scan  [--profile P] [--min-age DAYS] [-v] [--json]
    cna clean [--profile P] [--min-age DAYS] [--yes] [-v] [--json]

* `P` is a comma-separated list: `quick` (alias `safe`), `browser`, `dev`, `gaming`, `windows`, `apps`, `custom`,
  `full` (everything). Default `quick`.
* `scan` always previews. `clean` previews unless `--yes` is given (`--dry-run` also forces a preview).
* `--min-age DAYS` keeps files modified more recently than this, for every rule. Without it each rule uses its own
  default (1 day for temp-like rules, 7 for CBS logs, 0 otherwise).
* Files that are in use are skipped and counted; they never make the command fail hard (exit code 2 reports them).
* Rules that need administrator rights are skipped and listed ("Skipped (run as administrator)").

### Built-in rules

`%X%` means the environment variable. `*` matches one path segment (e.g. every browser profile).

| Profile | Rule id | Location (under) | Notes |
|---|---|---|---|
| quick | `user-temp` | `%TEMP%` | files < 1 day old kept |
| quick | `inet-cache` | `%LOCALAPPDATA%\Microsoft\Windows\INetCache` | files < 1 day old kept |
| quick | `thumbnails` | `%LOCALAPPDATA%\Microsoft\Windows\Explorer\thumbcache_*.db` | locked files skipped |
| quick | `crash-dumps` | `%LOCALAPPDATA%\CrashDumps` | |
| quick | `wer-user` | `%LOCALAPPDATA%\Microsoft\Windows\WER` | |
| browser | `chrome-cache`, `edge-cache`, `brave-cache`, `vivaldi-cache` | `...\User Data\*\{Cache, Code Cache, GPUCache, Service Worker\CacheStorage}` and `User Data\{ShaderCache, GrShaderCache}` | never touches bookmarks, passwords, history |
| browser | `firefox-cache` | `%LOCALAPPDATA%\Mozilla\Firefox\Profiles\*\{cache2, startupCache}` | |
| dev | `pip-cache` | `%LOCALAPPDATA%\pip\Cache` | |
| dev | `npm-cache` | `%LOCALAPPDATA%\npm-cache`, `%APPDATA%\npm-cache` | |
| dev | `yarn-cache` | `%LOCALAPPDATA%\Yarn\Cache` | |
| dev | `nuget-http` | `%LOCALAPPDATA%\NuGet\v3-cache` | |
| dev | `gradle-cache` | `%USERPROFILE%\.gradle\caches` | re-downloaded; first build is slow |
| dev | `cargo-registry` | `%USERPROFILE%\.cargo\registry\cache` | re-downloaded on next build |
| dev | `go-build` | `%LOCALAPPDATA%\go-build` | |
| dev | `vscode-cache` | `%APPDATA%\Code\{Cache, CachedData, Code Cache, GPUCache}` | |
| gaming | `nvidia-shaders` | `%LOCALAPPDATA%\NVIDIA\{DXCache, GLCache}`, `%PROGRAMDATA%\NVIDIA Corporation\NV_Cache` | shaders recompile -> brief stutter |
| gaming | `d3d-shaders` | `%LOCALAPPDATA%\D3DSCache` | same |
| gaming | `amd-shaders` | `%LOCALAPPDATA%\AMD\DxCache` | same |
| gaming | `steam-web` | `%PROGRAMFILES(X86)%\Steam\appcache\httpcache`, `%LOCALAPPDATA%\Steam\htmlcache` | |
| gaming | `epic-web` | `%LOCALAPPDATA%\EpicGamesLauncher\Saved\webcache*` | |
| windows | `win-temp` | `%SystemRoot%\Temp` | **admin**, files < 1 day old kept |
| windows | `win-update-dl` | `%SystemRoot%\SoftwareDistribution\Download` | **admin** |
| windows | `wer-system` | `%PROGRAMDATA%\Microsoft\Windows\WER` | **admin** |
| windows | `minidumps` | `%SystemRoot%\Minidump` | **admin** |
| windows | `cbs-logs` | `%SystemRoot%\Logs\CBS` | **admin**, files < 7 days old kept |
| apps | `discord-cache`, `slack-cache` | `%APPDATA%\{discord, Slack}\{Cache, Code Cache, GPUCache}` | |
| apps | `spotify-cache` | `%LOCALAPPDATA%\Spotify\{Data, Storage}` | |
| custom | `custom` | your `custom_rule` lines in `cna.conf` | protected locations are refused |

Targets nested inside other targets are merged, so nothing is counted or deleted twice.

### JSON

    {"mode":"dry-run|clean","total_bytes":N,"total_files":N,"failed":N,
     "rules":[{"rule":"id","bytes":N,"files":N,"failed":N,"paths":["..."]}],
     "skipped_needs_admin":["rule-id"]}

## `dupes`

    cna dupes [paths...] [--min-size 1M] [--max-size N] [--limit 25] [--no-skip]
              [--quarantine --yes] [--json]
    cna dupes --of FILE [paths...]

* With no paths: Downloads, Documents, Desktop, Pictures, Videos, Music of the current user (those that exist).
* `--min-size` default **1M**; accepts `K`, `M`, `G`, `T`. `--max-size` default unlimited.
* `--limit` limits the text listing (default 25 sets); JSON always lists all sets.
* `--no-skip` also scans the folders skipped by default (`.git`, `node_modules`, Recycle Bin, `Windows`, `WinSxS`,
  `Program Files*`, `ProgramData`, `System Volume Information`).
* Every set shows `keep` (oldest copy, ties -> shorter path) and `dupe` entries.
* `--quarantine --yes` moves all `dupe` entries to quarantine. The keeper is never moved.
* `--of FILE` lists other copies of that one file found under the given paths.
* Hard links and cloud placeholders are ignored (see `ARCHITECTURE.md`).

JSON:

    {"sets":[{"size":N,"wasted":N,"keeper":index,"paths":["..."]}],"total_wasted":N,"quarantined_files":N}

## `remnants`

    cna remnants [--min-score 65] [--apps-file FILE] [--quarantine --yes] [--json]
    cna remnants --app NAME [--depth 3] [--apps-file FILE] [--quarantine --yes] [--json]

**Without `--app`** - finds folders in AppData / ProgramData / Program Files that no installed app owns, with a
0-100 score and a reason, plus *stale uninstall entries* (registered apps whose install folder no longer exists;
report only). Needs an app inventory: the Windows registry, or `--apps-file` with one `name|publisher|install_dir`
per line.

**With `--app NAME`** - lists every file/folder whose name contains NAME (at least 3 letters/digits) in the usual
places, and on Windows matching registry keys and startup (`Run`) entries. Registry results are never modified.
`--quarantine` is refused while an app with that name still appears installed.

JSON (leftovers):

    {"apps_known":N,"remnants":[{"path":"...","kind":"...","score":N,"bytes":N,"reason":"..."}],
     "stale_uninstall_entries":["..."],"total_bytes":N,"quarantined_bytes":N}

JSON (`--app`):

    {"term":"...","still_installed":["..."],"paths":[{"path":"...","dir":true,"bytes":N}],
     "registry":["..."],"total_bytes":N}

## `explain`

    cna explain PATH [--refresh] [--llm] [--no-store] [--apps-file FILE] [--json]

Prints what the folder/file is, its kind and safety class (`regenerable`, `caution`, `keep`, `unknown`), the installed
app that owns it (if any), size, last change, content mix and the source of the answer (`rules`, `knowledge base`,
`local LLM`). Known answers are saved to the knowledge base unless `--no-store`; `--refresh` ignores the saved answer.
`--llm` is only effective in builds that provide an LLM backend (the default build has none).

JSON: `{"path","kind","safety","summary","owner","bytes","files","source"}`

## `kb`

    cna kb list
    cna kb search WORD [WORD...]     # all words must match path/kind/summary/safety
    cna kb forget PATH

JSON: `{"entries":[{"path","kind","safety","summary","source"}],"count":N}`

## `quarantine`

    cna quarantine list [--json]
    cna quarantine restore ID                    # refuses to overwrite an existing path
    cna quarantine purge --older-than DAYS --yes # or --all --yes; permanent

JSON (list): `{"entries":[{"id","original","bytes","when","reason"}],"total_bytes":N}`

## `config` and `auto`

    cna config path | init [--force] | show
    cna auto run [--force] [--dry-run] [--config FILE]
    cna auto install [--every-hours N]     # default 6; N >= 24 becomes a daily task
    cna auto remove
    cna auto status

See `CONFIG.md`. `auto run --json` prints `{"dry_run":bool,"summary":"..."}`.

## `recommend`

```text
cna recommend [--min-size 5M] [--limit 25] [--root PATH ...] [--apps-file F] [--apply --yes] [--json]
```

Looks through `%LOCALAPPDATA%`, `%APPDATA%` and `%PROGRAMDATA%` (or the `--root` folders) down to six levels and proposes **clean** (safe: moved to
quarantine), **review** (look first) or **keep** (hidden). Rules decide where they know the folder; otherwise the on-device model proposes and a
safety gate has the last word. Each row shows the source (`rules` / `model` / `your decision` / `safety rule`), the confidence and the reasons.
`--apply --yes` moves only the `clean` rows to quarantine. Details: [MODEL.md](MODEL.md).

JSON: `{"items":[{"path","name","action","class","confidence","bytes","source","note","because":[...]}],"clean_bytes","review_bytes","folders_checked","applied","quarantined","quarantined_bytes","failed"}`.

## `model`

```text
cna model status | eval | retrain | reset --yes
cna model predict PATH [--json]            class, confidence, action, reasons
cna model feedback PATH --label regenerable|caution|keep
cna model forget PATH
```

`eval` runs grouped 5-fold cross-validation on the built-in seed. `feedback` stores your decision and retrains (about 70 ms). Restoring something the
advisor quarantined records `caution` automatically. `predict --json`: `{"path","class","confidence","action","note","because":[...]}`.
`explain --json` now also contains `model_class`, `model_confidence` and `action`.

## `debloat`

```text
cna debloat scan [-v] [--json] [--packages-file F]
cna debloat apply [--preset safe|balanced|aggressive] [--only privacy,startup,services,disk,apps] [--apps] [--id ID ...] [--yes] [--json]
cna debloat journal [--json]
cna debloat undo (--all | --id LABEL) [--yes]
```

Preview by default; `--yes` applies. Presets never include Store apps unless `--apps` / `--only apps`. Everything except app removal is recorded in a
journal and reversible with `undo`. Full catalogue, JSON formats and caveats: [DEBLOAT.md](DEBLOAT.md).

## Examples

    # what would a full cleanup free, as JSON, for a script
    cna scan --profile full --json

    # clean, but never touch my project cache
    cna clean --profile dev,browser --exclude "C:\Users\me\AppData\Local\MyTool\Cache" --yes

    # is there another copy of this file anywhere in my media folders?
    cna dupes --of "D:\Media\wedding.mp4" D:\Media E:\Backup

    # review leftovers, then quarantine only the high-confidence ones
    cna remnants --min-score 80
    cna remnants --min-score 80 --quarantine --yes

    # undo
    cna quarantine list
    cna quarantine restore 20261004-193215-1

    # PowerShell: act on the exit code
    cna clean --profile quick --yes --quiet; if ($LASTEXITCODE -eq 2) { "some files were in use" }

```text
cna recommend                                   what could I clean on this PC, and why?
cna recommend --apply --yes                     move the suggested folders to quarantine
cna model feedback "%LOCALAPPDATA%\Foo\Data" --label regenerable
cna debloat scan                                preview privacy / startup / service / app changes
cna debloat apply --preset safe --yes           apply only the safe, undoable ones
cna debloat undo --all --yes                    put everything back
```
