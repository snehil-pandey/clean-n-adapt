# clean-n-adapt

Clean-n-Adapt is a Windows maintenance app I started as a small cache-clearing script for my own convenience back in 7th standard. It kept getting upgraded whenever my PC changed, and now it is becoming a proper desktop utility: clean UI first, safe cleanup engine underneath, and only a tiny command line surface for automation.

## Tags

`windows` `python` `pyside6` `qt` `nuitka` `inno-setup` `sqlite` `cleanup` `maintenance` `desktop-app`

## What it does

- Opens a desktop dashboard when `cna.exe` or `cna` is launched.
- Shows PC status, cleanup estimate, app count, startup count, disk free, memory free, and DB path.
- Gives clean widget-style actions for quick, browser, developer, gaming, Windows, and full cleanup previews.
- Keeps scans cached in SQLite so the app does not need to rescan everything every time.
- Tracks installed apps in a deduplicated inventory.
- Runs safe boost actions like DNS flush, Store reset, Disk Cleanup, and power-plan switching.
- Adds a `refresh` action that restarts `explorer.exe` and sends `Win + Ctrl + Shift + B`.
- Keeps runtime state beside the app in `.state`.

## Command Line

The CLI is intentionally small now. The normal experience is the desktop app.

```powershell
cna
cna gui
cna clean --dry-run
cna clean --mode browser --dry-run
cna clean --mode quick --yes
cna boost
cna boost dns
cna boost store
cna boost disk
cna boost power
cna refresh
cna --version
```

Allowed command groups:

| Command | Purpose |
| --- | --- |
| `cna` | Opens the desktop app. |
| `cna gui` | Opens the desktop app explicitly. |
| `cna clean` | Previews or runs cleanup. |
| `cna boost` | Runs Windows maintenance boost actions. |
| `cna refresh` | Restarts Explorer and triggers graphics-driver refresh hotkey. |

Cleanup modes:

```powershell
cna clean --mode quick --dry-run
cna clean --mode safe --dry-run
cna clean --mode browser --dry-run
cna clean --mode dev --dry-run
cna clean --mode gaming --dry-run
cna clean --mode windows --dry-run
cna clean --mode full --dry-run
```

Deletion requires `--yes`. Without `--yes`, cleanup stays in preview mode.

## Desktop UI

The desktop app is built with PySide6/Qt. It is meant to feel like a small professional Windows utility, not a terminal wrapped in a window.

Main areas:

| Screen | Details |
| --- | --- |
| Dashboard | Health, junk estimate, apps, startup, disk, memory, DB location. |
| Clean | Widget buttons for safe cleanup modes. |
| Boost | DNS, Store, Disk Cleanup, power plan, safe boost set. |
| Apps | Cached installed-app inventory. |
| Monitor | Current disk, memory, indexed cleanup, Downloads summary. |
| Settings | Install path, download location, PATH scope, shortcut preference. |

There is also a tray menu for quick preview, shell refresh, and opening the dashboard.

## Refresh

`cna refresh` does two things:

1. Restarts `explorer.exe`.
2. Sends the Windows graphics reset shortcut: `Win + Ctrl + Shift + B`.

This is useful when the shell, taskbar, display driver, or desktop UI feels stuck.

## Boost

Boost does not magically overclock the PC. It only wraps safe Windows maintenance actions:

| Action | What happens |
| --- | --- |
| `dns` | Runs `ipconfig /flushdns`. |
| `store` | Runs `wsreset.exe`. |
| `disk` | Launches Windows Disk Cleanup. |
| `power` | Switches to the high-performance power plan when available. |
| `all` | Runs DNS flush, Store reset, and Disk Cleanup. |

## Install

The release should provide either a setup EXE or a ZIP package.

Preferred installer:

```text
Clean-n-Adapt-Setup-2.0.0.exe
```

The installer is designed to:

- Ask for UAC/admin access.
- Let the user choose the install folder.
- Install into `C:\Program Files\cleanNadapt` by default.
- Create `.state` inside the install folder.
- Add Clean-n-Adapt to PATH.
- Register App Paths for `cna.exe`.
- Create Start Menu shortcuts.
- Optionally create a Desktop shortcut.
- Launch the app after setup.

ZIP/dev install still works when needed:

```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1
```

From a repo checkout, build first:

```bat
scripts\build-exe.cmd
scripts\install.cmd
```

## Build

PyInstaller is no longer used. The app is built with Nuitka.

PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-exe.ps1
```

CMD:

```bat
scripts\build-exe.cmd
```

Output:

```text
dist\cna.exe
```

Installer script:

```text
scripts\clean-n-adapt.iss
```

Compile it with Inno Setup to create:

```text
installer\Clean-n-Adapt-Setup-2.0.0.exe
```

## State

Runtime state is stored beside the installed executable:

```text
<install folder>\.state\clean-n-adapt.db
```

That database stores scan results, app inventory, settings, history, and cleanup results. It does not store file contents.

## Safety

- Cleanup previews by default.
- Actual deletion requires explicit confirmation through the UI or `--yes` in CLI automation.
- Admin-only paths are skipped unless the app is elevated.
- App removal remains official-uninstaller-only.
- Scan/index data is cached and refreshed intentionally.
- Dangerous roots and important system folders are blocked by the cleanup engine.

## Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md).
