# clean-n-adapt

Windows cleanup, app inventory, uninstall helper, maintenance CLI, and interactive dashboard.

I made the first version as a small cache-clearing script for my own convenience back in 7th standard. It kept growing as my PCs changed, and it is now a proper Windows utility with a SQLite state DB, safe cleanup modes, custom rules, app inventory, reports, and a Rich-based CLI/TUI.

## Tags

`windows` `python` `cli` `tui` `rich` `sqlite` `cleanup` `maintenance` `pyinstaller` `system-utility`

## Tech

- Python
- Rich
- SQLite
- PyInstaller
- PowerShell
- CMD/batch

## Contents

- [Install](#install)
- [Build](#build)
- [Quick Start](#quick-start)
- [Commands](#commands)
- [Diagnose and Advise](#diagnose-and-advise)
- [Custom Locations](#custom-locations)
- [Apps](#apps)
- [Boost](#boost)
- [State and Storage](#state-and-storage)
- [Safety](#safety)
- [Architecture](#architecture)

## Install

Download the Windows ZIP from the release page and extract it. Use `v1.2.2` or newer for the native-feeling installer and admin-aware EXE. The ZIP already includes `cna.exe`; the installer does not rebuild from source.

By default, the installer targets:

```text
C:\Program Files\cleanNadapt
```

If needed, it relaunches with UAC. The EXE is built with an administrator manifest, so protected Windows maintenance asks for elevation instead of failing later.

After install, this file should exist:

```text
C:\Program Files\cleanNadapt\cna.cmd
```

Direct check:

```powershell
& "C:\Program Files\cleanNadapt\cna.cmd" --version
```

PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1
```

CMD:

```bat
install.cmd
```

From a repo checkout, build first so `dist\cna.exe` exists:

```bat
scripts\build-exe.cmd
scripts\install.cmd
```

To install/copy into a custom folder:

```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1 -InstallDir "D:\Tools\clean-n-adapt"
```

```bat
install.cmd "D:\Tools\clean-n-adapt"
```

Installer integration:

- Adds install folder to PATH.
- Creates `cna.cmd`.
- Creates `.state` beside the executable.
- Creates a Start Menu shortcut.
- Can create a Desktop shortcut with `-DesktopShortcut`.
- Adds a Windows Terminal profile fragment when possible.
- Registers an uninstall entry.

The state database is stored relative to the executable:

```text
<install folder>\.state\clean-n-adapt.db
```

If the folder is moved, the app keeps using `.state` beside `cna.exe`. To override this manually:

```powershell
$env:CNA_STATE_DIR="D:\somewhere\clean-n-adapt-state"
```

If `cna` is not found after installing, add the install folder to PATH:

```powershell
powershell -ExecutionPolicy Bypass -File .\add-to-path.ps1
```

```bat
add-to-path.cmd
```

Already-open terminals may not pick up PATH changes. Open a new terminal, or run the launcher directly:

```powershell
.\cna.cmd --version
```

## Build

PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-exe.ps1
```

CMD:

```bat
scripts\build-exe.cmd
```

Build output:

```text
dist\cna.exe
```

## Source Layout

The repo is named `clean-n-adapt`, but the Python package is `clean_n_adapt` inside `src/`.
Python import/package names cannot use hyphens, so the underscore package name is intentional.

## Quick Start

```powershell
cna
cna tui
cna help --all
cna ui
cna doctor
cna health
cna scan --refresh
cna clean quick --dry-run
cna apps scan --refresh
cna boost --startup
```

## Commands

### Help and Status

| Command | What it does |
| --- | --- |
| `cna` | Shows the home screen and common commands. |
| `cna tui` | Opens the interactive dashboard. |
| `cna help` | Shows the command helper. |
| `cna help --all` | Shows detailed command help. |
| `cna ui` | Opens the Rich menu UI. |
| `cna status` | Shows dashboard status. |
| `cna status --compact` | Prints one-line status. |
| `cna status --json` | Prints machine-readable status JSON. |
| `cna doctor` | Prints rule-based recommendations. |
| `cna health` | Shows an offline PC health score. |
| `cna permissions` | Checks admin, PATH, registry, scheduler, restore point, and Windows Terminal integration. |

### Diagnose and Advise

| Command | What it does |
| --- | --- |
| `cna doctor` | Analyzes cache index, disk space, startup entries, and app inventory, then recommends commands. |
| `cna health` | Shows total health score plus storage, startup, cache, maintenance, and app scores. |
| `cna startup` | Lists registry startup entries with estimated impact. |
| `cna startup disable NAME --yes` | Disables one matched startup entry. Previewed without `--yes`. |
| `cna storage top C:\` | Shows largest folders under a path and records a storage sample. |
| `cna storage history` | Shows recorded free-space history. |
| `cna browsers` | Shows browser cache usage from the cache index. |
| `cna downloads audit` | Audits old archives, installers, images, and duplicate names in Downloads. |
| `cna duplicates scan PATH` | Finds duplicate files by size and SHA-256 hash. Preview only in v1.1. |
| `cna snapshot create` | Saves apps, startup entries, and cache index snapshot. |
| `cna snapshot compare` | Compares the two latest snapshots. |
| `cna schedule weekly` | Creates/updates a Windows Task Scheduler maintenance task. |
| `cna restore-point` | Requests a Windows restore point before risky maintenance. |

`cna doctor` is not an LLM. It is an offline rule engine. Example advice:

```text
Browser cache is large -> cna clean browser --dry-run
Startup impact looks high -> cna startup
Disk free space is low -> cna storage top C:\
```

### Scan and Clean

| Command | What it does |
| --- | --- |
| `cna scan --refresh` | Refreshes the built-in cache/temp index. |
| `cna scan --refresh --include-admin` | Includes admin-only Windows cache locations when elevated. |
| `cna clean quick` | Cleans safe indexed locations. |
| `cna clean quick --dry-run` | Previews quick clean without deleting. |
| `cna clean deep` | Uses deeper indexed categories with extra caution. |
| `cna clean browser` | Cleans browser caches only. |
| `cna clean dev` | Cleans developer caches only. |
| `cna clean gaming` | Cleans game/shader/launcher caches only. |
| `cna clean windows` | Cleans Windows cache/temp entries. |
| `cna clean custom` | Cleans enabled custom rules after preview. |
| `cna clean full` | Runs all built-in modes and custom rules. |
| `cna cache clear` | Compatibility alias for safe clean. |

The built-in scan checks obvious Windows/browser temp locations and bounded deep discovery up to 10 directories deep under common AppData, ProgramData, and project roots. It searches cache-like folders such as `Cache`, `Code Cache`, `GPUCache`, `ShaderCache`, `LocalCache`, `TempState`, `.cache`, `.pytest_cache`, and `__pycache__`.

## Custom Locations

Add a folder:

```powershell
cna custom add "D:\SomeApp\cache" --name "SomeApp cache" --category "App Cache" --recursive --risk caution
```

Add a glob:

```powershell
cna custom add "D:\Builds" --type glob --pattern "*.tmp" --category Dev --recursive --min-age-hours 24
```

Manage rules:

```powershell
cna custom list
cna custom show 1
cna custom edit 1 --notes "Unreal build temp" --min-age-hours 168
cna custom disable 1
cna custom enable 1
cna custom remove 1
```

Preview and clean:

```powershell
cna custom preview
cna custom preview 1
cna custom clean --dry-run
cna custom clean 1
```

Custom cleanup always previews and asks before deleting.

## Apps

App data is scanned once and stored in SQLite. Listing uses the cached DB until refresh is requested.

| Command | What it does |
| --- | --- |
| `cna apps scan --refresh` | Refreshes cached app inventory. |
| `cna apps list` | Lists cached apps. |
| `cna apps list --refresh` | Refreshes inventory before listing. |
| `cna apps list --query brave` | Searches cached app inventory. |
| `cna apps uninstall "Brave"` | Launches the official uninstaller only. |
| `cna apps uninstall "Brave" --dry-run` | Shows what would happen without launching. |
| `cna apps uninstall` | Opens an interactive app picker. |

Apps are sorted by system, user, and Windows Store style packages.
Duplicate app identities are normalized so repeated registry/store entries collapse into one row when possible.

## Boost

| Command | What it does |
| --- | --- |
| `cna boost --dns` | Runs `ipconfig /flushdns`. |
| `cna boost --store` | Resets Microsoft Store cache. |
| `cna boost --disk-cleanup` | Runs Windows Disk Cleanup. |
| `cna boost --high-performance` | Switches to high-performance power plan. |
| `cna boost --startup` | Lists startup registry entries only. |
| `cna boost --all` | Runs the safe boost set: DNS, Store reset, Disk Cleanup. |

## Monitor, History, and Reports

```powershell
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

## State and Storage

clean-n-adapt stores runtime data in SQLite:

```text
<app folder>\.state\clean-n-adapt.db
```

The database stores scan results, app inventory, settings, custom rules, history, and cleanup results. It does not store file contents.

## Safety

- Blocks dangerous roots like `C:\`, `C:\Windows`, `C:\Program Files`, and the user profile root.
- Important-looking custom paths require `--advanced`.
- Locked files are skipped.
- Admin paths are skipped unless the shell is elevated.
- Custom cleanup never runs silently.
- App uninstall uses official uninstall commands only.
- Deep discovery is bounded and skips runtime/tool/virtual environment folders.

## Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md).
