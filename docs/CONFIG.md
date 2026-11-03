# Configuration and automation

Automation is driven by one file, `cna.conf`, in the state directory (`cna config path` prints the location).

**Where is the state directory?** For an installed copy it is `%LOCALAPPDATA%\cna`. Order of precedence:
`--state DIR`, environment variable `CNA_HOME`, a `.state` folder that already exists next to `cna.exe`
(*portable mode* - create the folder yourself to use it, e.g. when running from a USB stick), then
`%LOCALAPPDATA%\cna`. It holds `cna.conf`, `kb.tsv`, `cna.log`, `quarantine\` and `widget.ini`, and is never
deleted by an upgrade; the uninstaller keeps it unless you explicitly answer Yes to its question.
Create a commented default with:

    cna config init            # add --force to overwrite
    cna config show

## Format

    key = value          # '#' starts a comment; blank lines are ignored; keys are case-insensitive

Unknown keys are an error (reported with the line number) so typos cannot silently disable a safety setting.
Booleans accept `true/false`, `yes/no`, `on/off`, `1/0`.

## Keys

| Key | Default | Meaning |
|---|---|---|
| `clean` | `true` | Run the cache cleaner. |
| `profiles` | `quick,browser,dev` | Cleaning profiles (`quick browser dev gaming windows apps custom full`). |
| `min_age_days` | `3` | Keep files modified within this many days. **Overrides** every rule's own default. |
| `remnants` | `report` | Leftover folders: `off`, `report` (count only), or `quarantine` (move to quarantine). |
| `remnants_min_score` | `75` | Only act on leftovers with at least this score (0-100). |
| `dupes` | `off` | Duplicate files: `off`, `report`, or `quarantine`. |
| `dupes_roots` | *(empty)* | Folders to search, separated by `;`. Empty = the user's Downloads, Documents, Desktop, Pictures, Videos, Music. |
| `dupes_min_size` | `50M` | Ignore smaller files (`K`, `M`, `G`). |
| `quarantine_keep_days` | `30` | At the end of each run, **permanently delete** quarantined items older than this. `0` keeps them forever. |
| `min_hours_between_runs` | `12` | Skip the run if the last one was more recent (bypass with `--force`). |
| `free_below_percent` | `0` | If > 0, run only when free space on the drive holding your user profile is below this percentage. `0` = always run. |
| `mem_cap_mb` | `125` | Memory cap for the run. It can only **lower** the cap given on the command line (minimum 16). |
| `exclude` | - | A path `cna` must never touch (repeatable). |
| `custom_rule` | - | An extra cache location for the `custom` profile (repeatable). Supports `${ENV}` variables and `*` / `?` per path segment. Protected locations are refused. |

Paths may use `/` or `\`.

## Order of a run

`auto run` performs, in order:

1. **Gating** - skipped (exit 0, with a message unless `--quiet`) if the last run is too recent or free space is not
   below the trigger. `--force` bypasses gating.
2. **Clean** - the configured profiles with `min_age_days`.
3. **Leftovers** - needs the Windows app inventory; reported or quarantined according to `remnants`.
4. **Duplicates** - reported or quarantined according to `dupes`.
5. **Quarantine retention** - items older than `quarantine_keep_days` are purged.
6. **Bookkeeping** - `state.txt` is updated and one summary line is appended to `cna.log`.

`--dry-run` executes steps 2-4 as previews only and skips steps 5-6.

Exit codes follow `CLI.md` (2 if some items could not be moved or deleted).

## Scheduling

    cna auto install --every-hours 6      # creates the per-user Task Scheduler task "cna-auto"
    cna auto status                       # config path, last run, whether the task exists
    cna auto remove

The task runs `"<path>\cna.exe" auto run --quiet` every N hours (N >= 24 becomes a daily task every N/24 days).
It is created for the current user without administrator rights, so it runs the profiles that do not need elevation.
`auto install` creates a default `cna.conf` first if none exists. Because the task points at the exe's current path,
run `auto install` again after moving the folder.

You can also schedule it yourself:

    schtasks /Create /TN "cna-auto" /TR "\"C:\Tools\cna\cna.exe\" auto run --quiet" /SC DAILY /ST 03:00

## Files written

| File (state directory) | Content |
|---|---|
| `model.bin` | the on-device folder model (196,640 bytes); rebuilt automatically if damaged or outdated |
| `feedback.tsv` | your decisions about folders (explicit feedback and restores from quarantine) |
| `debloat-journal.tsv` | previous values of every debloat change, used by `debloat undo` |
| `state.txt` | `last_run=<epoch>`, `last_freed=<bytes>` |
| `cna.log` | one line per run: `2026-10-04 19:33:23 \| cleaned 4.9 KB (1 files); remnants: 3 found (120 MB)` (rotates to `cna.log.1` at 256 KB) |

## Example configurations

**Conservative (default spirit)** - only cleans caches, only reports everything else:

    clean = true
    profiles = quick,browser
    min_age_days = 3
    remnants = report
    dupes = off
    min_hours_between_runs = 24

**Developer machine** - reclaim build caches, keep a week of slack, never touch one tool's cache:

    profiles = quick,browser,dev
    min_age_days = 7
    exclude = C:/Users/me/AppData/Local/pip/Cache/keep-this
    custom_rule = ${LOCALAPPDATA}/MyCompiler/Cache

**Low disk space safety valve** - run often, but only act when the drive is nearly full:

    profiles = quick,browser,dev,gaming
    min_hours_between_runs = 1
    free_below_percent = 15
    remnants = quarantine
    remnants_min_score = 85
    quarantine_keep_days = 14

## Troubleshooting

* `config error: line N: unknown key 'x'` - fix the typo; nothing runs until the file parses.
* `Skipped: last run was N minute(s) ago` - lower `min_hours_between_runs` or use `--force`.
* `remnants: no app inventory` in the log - the app list comes from the Windows registry; on non-Windows hosts it is empty.
* Windows-profile rules never run from the scheduled task because it is not elevated. Run `cna clean --profile windows --yes`
  from an administrator prompt when needed.
