# clean-n-adapt

My Windows cleanup + maintenance CLI.

This started as a `clear cache.bat`, but the batch version was too blunt. This repo is the proper version: it keeps a SQLite index, has a real CLI, supports custom cleanup rules, and refuses sketchy deletes.

Repo:

```text
https://github.com/snehil-pandey/clean-n-adapt
```

## What it does

- dashboard/status for disk, memory, admin state, DB, scan age, and indexed cleanup size
- quick/deep/mode-based cleaning
- custom cleanup rules for folders, files, and glob patterns
- app list/search/uninstall through official uninstall commands only
- boost actions like DNS flush, Store reset, Disk Cleanup, power plan, and startup listing
- monitor view for live disk/memory/cache state
- history and report export
- settings saved in SQLite

Things intentionally not done:

- no registry hive deletion
- no raw event log file deletion
- no random `Program Files` app deletion
- no silent custom cleanup

## Install

PowerShell:

```powershell
git clone https://github.com/snehil-pandey/clean-n-adapt.git
cd clean-n-adapt
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
python -m pip install -e .
cna
```

If PowerShell blocks scripts:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install.ps1
```

CMD:

```bat
git clone https://github.com/snehil-pandey/clean-n-adapt.git
cd clean-n-adapt
scripts\install.cmd
```

## Build exe

```bat
scripts\build-exe.cmd
```

Output:

```text
dist\cna.exe
```

## Main commands

Open the dashboard:

```powershell
cna
cna status
cna status --compact
cna status --json
```

Open the basic Rich menu UI:

```powershell
cna ui
```

Scan and update the DB:

```powershell
cna scan --refresh
```

Clean modes:

```powershell
cna clean quick
cna clean deep
cna clean safe
cna clean browser
cna clean dev
cna clean gaming
cna clean windows
cna clean custom
cna clean full
```

Dry run:

```powershell
cna clean quick --dry-run
cna clean full --dry-run
```

Old alias still works:

```powershell
cna cache clear --dry-run
```

## Custom locations

Add a folder rule:

```powershell
cna custom add "D:\SomeApp\cache" --name "SomeApp cache" --category "App Cache" --recursive --risk caution
```

Add a glob rule:

```powershell
cna custom add "D:\Builds" --type glob --pattern "*.tmp" --category Dev --recursive --min-age-hours 24
```

List/preview/clean:

```powershell
cna custom list
cna custom preview
cna custom preview 1
cna custom clean --dry-run
cna custom clean 1
```

Manage rules:

```powershell
cna custom show 1
cna custom edit 1 --notes "Unreal build temp" --min-age-hours 168
cna custom disable 1
cna custom enable 1
cna custom remove 1
```

Custom cleanup always previews and asks before deleting. `--yes` is not used for custom cleaning.

Safety checks block drive roots, Windows, Program Files, ProgramData root, and the user profile root. Important-looking paths like Desktop/Documents/repos need `--advanced`, and even then the tool still previews first.

## Apps

```powershell
cna apps list
cna apps list --query brave
cna apps uninstall "Brave"
cna apps uninstall "Brave" --dry-run
```

Uninstall means official uninstall command or nothing. After an uninstall, the tool only previews known leftover-looking locations.

## Boost / monitor / history / reports

```powershell
cna boost --dns --store --disk-cleanup
cna boost --startup
cna boost --all

cna monitor --compact --interval 5 --count 6
cna monitor --refresh-each --interval 60

cna history
cna report --format txt
cna report --format json
```

## Settings

```powershell
cna settings list
cna settings set cache_warning_bytes 1073741824
```

DB location:

```text
%LOCALAPPDATA%\clean-n-adapt\clean-n-adapt.db
```

Override DB/state folder:

```powershell
$env:CNA_STATE_DIR="D:\somewhere\state"
```

## Notes

- Locked files are skipped.
- Admin paths are skipped unless the shell is elevated.
- Reports do not include file contents.
- The CLI is scriptable; the `ui` command is just a friendlier menu over the same actions.
