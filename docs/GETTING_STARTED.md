# Getting Started

Clean-n-Adapt is a Windows maintenance utility made of two native programs:

| Program | Use it for |
| --- | --- |
| `cna.exe` | Command-line scans, cleanup, duplicate review, quarantine, advice, and maintenance actions. |
| `cna-widget.exe` | The compact desktop widget and tray menu. It runs `cna.exe` beside it. |

The installer is the normal way to use the app. The loose EXEs are useful for testing or portable use, but they must stay together in the same folder.

## First Run

1. Install `Clean-n-Adapt-Setup-<version>.exe` for the current Windows user.
2. Open **Clean-n-Adapt** from Start to use the widget, or open **Clean-n-Adapt command line** for `cna`.
3. Run a preview first. Every cleanup workflow should begin with a scan or a command without `--yes`.

```text
cna scan --profile quick,browser
cna dupes D:\Downloads --min-size 1G
cna remnants --min-score 80
cna recommend
cna debloat scan
```

`--yes` is the explicit permission flag. Without it, cleanup, quarantine, purge, recommendation application, and debloat application stay in preview mode.

## Everyday Tasks

### Clean disposable cache files

```text
# Preview the normal low-risk cache targets.
cna scan --profile quick,browser

# Delete only after reviewing the preview.
cna clean --profile quick,browser --yes
```

Use `--profile dev` for dependency/build caches, `--profile gaming` for shader and launcher cache, and `--profile windows` only in an elevated terminal. `full` combines all profiles, so it should be previewed before use.

Files in use are skipped. Browser bookmarks, passwords, history, user documents, saved games, and protected roots are not built-in cleanup targets.

### Find duplicate files

```text
cna dupes D:\Downloads E:\Archive --min-size 100M
cna dupes D:\Downloads E:\Archive --min-size 100M --quarantine --yes
cna quarantine list
cna quarantine restore <id>
```

The tool verifies content with SHA-256 before reporting a duplicate. Quarantine moves extra copies and keeps one selected copy in place. Permanent deletion is a separate action:

```text
cna quarantine purge --older-than 30 --yes
```

### Review leftovers from removed apps

```text
cna remnants --min-score 80
cna explain "C:\Users\you\AppData\Local\OldApp"
cna remnants --min-score 80 --quarantine --yes
```

`remnants` is conservative: it reports candidates first. Registry matches are report-only, and a folder belonging to an installed app cannot be quarantined through this command.

### Use the recommendation engine

```text
cna recommend
cna recommend --min-size 500M
cna recommend --apply --yes
```

Recommendations marked `clean` are moved to quarantine, not directly deleted. `review` means inspect it first. Feed decisions back when a suggestion is wrong:

```text
cna model feedback "C:\path\to\folder" --label keep
cna model feedback "C:\path\to\folder" --label regenerable
```

### Debloat carefully

```text
cna debloat scan
cna debloat apply --preset safe
cna debloat apply --preset safe --yes
cna debloat journal
cna debloat undo --all --yes
```

`safe`, `balanced`, and `aggressive` are increasingly broad presets. Service changes and hibernation need an elevated terminal. Store-app removal is never part of a default preset; it requires `--apps` and cannot be undone by Clean-n-Adapt.

Run debloat on a spare profile or restore point first. Read the listed effect and cost for every item. Do not run `tests/debloat_check.py` on Windows until the fake-registry backend is implemented for the Windows build; see [TESTING.md](TESTING.md).

## State, Quarantine, and Portable Use

By default the installed app stores settings, the knowledge base, logs, debloat journal, and quarantine at:

```text
%LOCALAPPDATA%\cna
```

To use a portable state folder, create `.state` beside `cna.exe`. You can also set a specific state location for one command:

```text
cna --state D:\CNA-State scan --profile quick
```

The uninstaller keeps `%LOCALAPPDATA%\cna` unless you explicitly choose to remove it. Keep that directory while you still need to restore quarantined files or undo debloat settings.

## Automation

Create a config file before enabling automation:

```text
cna config init
cna config show
cna auto run --dry-run --force
cna auto install --every-hours 6
cna auto status
```

Start with `quick,browser` cleanup only. Enable remnants or duplicate quarantine only after reviewing their reports. Remove the scheduled task with `cna auto remove`.

## Helpful Global Flags

| Flag | Purpose |
| --- | --- |
| `--yes` / `-y` | Allows a modifying action. |
| `--json` | Produces machine-readable output. |
| `--verbose` / `-v` | Shows matched paths and added detail. |
| `--exclude PATH` | Keeps a path and everything below it out of the operation. Repeatable. |
| `--state DIR` | Uses a specific state directory. |
| `--mem-cap MB` | Changes the memory budget; minimum is 16 MB, default is 125 MB. |

For the full command and JSON reference, use [CLI.md](CLI.md). For safety boundaries and recovery, read [SAFETY.md](SAFETY.md).
