# clean-n-adapt

My Windows cleanup + maintenance CLI.

I started this because my old `clear cache.bat` was doing too much in a very dumb way. It worked, but every run scanned/deleted the same stuff again and some actions were too risky to keep as raw batch commands.

This version keeps a small SQLite index, uses a proper CLI, and only touches known cleanup locations.

Repo:

```text
https://github.com/snehil-pandey/clean-n-adapt
```

## What it does

- clears common temp/cache folders
- remembers scan results in SQLite so every cleanup does not need a full scan
- lists installed apps from Windows uninstall registry entries
- uninstalls apps only through their official uninstall command
- refuses app deletion when it cannot do it cleanly
- has boost commands for simple Windows maintenance stuff
- has status/monitor commands so I can see what the PC looks like without cleaning anything

Things from the old batch file that are covered:

- DNS flush
- user temp cleanup
- Windows temp cleanup when elevated
- thumbnail/icon cache cleanup
- browser cache cleanup for common Chromium/Firefox locations
- Windows Store reset
- Disk Cleanup
- Windows Update download cache when elevated

Things I am not keeping from the old batch file:

- deleting registry hive files
- deleting raw event log files
- deleting random system logs directly
- manually removing app folders when Windows does not provide an uninstaller

Those are not worth breaking a machine over.

## DB location

Default:

```text
%LOCALAPPDATA%\clean-n-adapt\clean-n-adapt.db
```

Override it:

```powershell
$env:CNA_STATE_DIR="D:\somewhere\state"
```

## Install

PowerShell:

```powershell
git clone https://github.com/snehil-pandey/clean-n-adapt.git
cd clean-n-adapt
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
python -m pip install -e .
cna --help
```

If PowerShell blocks scripts on the PC:

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

CMD usually works without fighting execution policy:

```bat
scripts\build-exe.cmd
```

PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-exe.ps1
```

Output:

```text
dist\cna.exe
```

## Commands I use

Fresh scan and save it to the DB:

```powershell
cna scan --refresh
```

Clean using the DB index:

```powershell
cna cache clear
```

Force scan before cleaning:

```powershell
cna cache clear --refresh
```

Dry run:

```powershell
cna cache clear --dry-run
```

Include admin-only cleanup locations:

```powershell
cna cache clear --include-admin
```

One-line status:

```powershell
cna status --compact
```

JSON status:

```powershell
cna status --json
```

Monitor every 5 seconds, 6 updates:

```powershell
cna monitor --compact --interval 5 --count 6
```

Monitor and refresh scan every time:

```powershell
cna monitor --refresh-each --interval 60
```

List apps:

```powershell
cna apps list
```

Search apps:

```powershell
cna apps list --query brave
```

Uninstall carefully:

```powershell
cna apps uninstall "Brave"
```

Boost basics:

```powershell
cna boost --dns --store --disk-cleanup
```

Run the safe boost set:

```powershell
cna boost --all
```

Use the exe:

```powershell
.\dist\cna.exe status --compact
.\dist\cna.exe scan --refresh
.\dist\cna.exe cache clear --dry-run
.\dist\cna.exe monitor --compact --interval 5 --count 3
```

## Notes

- Cache cleanup only targets known disposable places.
- Locked files get skipped.
- Admin paths are skipped unless the shell is elevated.
- App uninstall means official uninstaller or nothing.
- The DB stores scan metadata, not file contents.
