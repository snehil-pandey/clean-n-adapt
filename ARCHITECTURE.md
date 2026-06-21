# clean-n-adapt Architecture

clean-n-adapt is moving from a CLI-first utility to a desktop-first Windows maintenance app. The CLI remains, but only for the small automation surface: `clean`, `boost`, and `refresh`.

## Principles

- The desktop UI is the main product.
- The CLI is intentionally small and scriptable.
- Cleanup, boost, app inventory, monitor, and settings live in reusable core modules.
- Scans are cached in SQLite and refreshed intentionally.
- Runtime state stays beside the installed executable in `.state`.
- Build output comes from Nuitka, not PyInstaller.
- Installer packaging is handled by Inno Setup.

## Product Flow

```mermaid
flowchart TD
    User["User"] --> Launch["cna.exe / cna"]
    Launch --> Desktop["PySide6 desktop app"]
    Launch --> CLI["Small CLI surface"]
    Desktop --> Core["Core engine"]
    CLI --> Core
    Core --> DB["SQLite .state database"]
    Core --> Windows["Windows filesystem, registry, shell tools"]
```

## UI Layer

```mermaid
flowchart TD
    Desktop["CleanNAdaptWindow"] --> Sidebar["Navigation sidebar"]
    Desktop --> Stack["Screen stack"]
    Desktop --> Tray["System tray menu"]
    Stack --> Dashboard["Dashboard"]
    Stack --> Clean["Clean widgets"]
    Stack --> Boost["Boost widgets"]
    Stack --> Apps["Apps inventory"]
    Stack --> Monitor["Monitor"]
    Stack --> Settings["Settings"]
    Tray --> QuickClean["Quick clean preview"]
    Tray --> ShellRefresh["Refresh shell"]
```

## CLI Layer

```mermaid
flowchart TD
    CLI["argparse router"] --> NoArgs["no args opens desktop"]
    CLI --> Clean["clean"]
    CLI --> Boost["boost"]
    CLI --> Refresh["refresh"]
    Clean --> CleanMode["clean_mode(mode, dry_run, yes)"]
    Boost --> BoostAction["run_boost(kind)"]
    Refresh --> ShellAction["refresh_windows_shell()"]
```

## Core Engine

```mermaid
flowchart TD
    Core["Core engine"] --> Actions["actions.py"]
    Core --> Cleaner["cleaner.py"]
    Core --> Targets["targets.py"]
    Core --> Apps["apps.py"]
    Core --> Diagnostics["diagnostics.py"]
    Core --> Monitor["monitor.py"]
    Core --> Boost["boost.py"]
    Core --> DBLayer["db.py"]
    Actions --> Cleaner
    Actions --> Boost
    Actions --> Targets
    Cleaner --> DBLayer
    Apps --> DBLayer
    Monitor --> DBLayer
```

## Cleanup Flow

```mermaid
flowchart TD
    UserAction["UI widget or cna clean"] --> LoadIndex["Load recent scan index"]
    LoadIndex --> Missing{"Index missing or stale?"}
    Missing -->|yes| Refresh["Refresh cache index"]
    Missing -->|no| Filter["Filter by cleanup mode"]
    Refresh --> Filter
    Filter --> Preview{"Preview only?"}
    Preview -->|yes| Show["Show size and location count"]
    Preview -->|no| Delete["Delete selected disposable items"]
    Delete --> History["Write history/results"]
    Show --> History
```

## Refresh Flow

```mermaid
flowchart TD
    Refresh["cna refresh / UI refresh"] --> Explorer["taskkill explorer.exe"]
    Explorer --> Restart["start explorer.exe"]
    Restart --> Hotkey["Send Win + Ctrl + Shift + B"]
    Hotkey --> History["Record refresh action"]
```

## App Inventory

```mermaid
flowchart TD
    Apps["Apps screen"] --> Inventory["installed_apps()"]
    Inventory --> Registry["Registry uninstall keys"]
    Inventory --> Store["Windows Store style entries"]
    Registry --> Normalize["Normalize AppIdentity"]
    Store --> Normalize
    Normalize --> Dedupe["Merge duplicate sources"]
    Dedupe --> DB["SQLite app_inventory"]
    DB --> UI["Apps list"]
```

## Build and Installer

```mermaid
flowchart TD
    Source["Source checkout"] --> Venv[".venv"]
    Venv --> Nuitka["Nuitka onefile standalone build"]
    Nuitka --> Exe["dist/cna.exe"]
    Exe --> Inno["Inno Setup script"]
    Inno --> Setup["Clean-n-Adapt-Setup.exe"]
    Setup --> InstallDir["Chosen install directory"]
    InstallDir --> State[".state"]
    InstallDir --> Path["PATH/App Paths"]
    InstallDir --> Shortcuts["Start Menu/Desktop"]
```

## State Database

```mermaid
flowchart LR
    DB["clean-n-adapt.db"] --> Scans["scans"]
    DB --> Apps["app_inventory"]
    DB --> Settings["settings"]
    DB --> History["history"]
    DB --> Results["cleanup_results"]
    Scans --> Dashboard["Dashboard junk estimate"]
    Apps --> AppsUI["Apps screen"]
    Settings --> InstallerUI["Settings screen"]
    History --> Activity["Activity log/reports"]
    Results --> Monitor["Monitor"]
```

## Package Layout

```mermaid
flowchart TD
    Repo["clean-n-adapt"] --> Src["src/clean_n_adapt"]
    Src --> Desktop["desktop.py"]
    Src --> CLI["cli.py"]
    Src --> Actions["actions.py"]
    Src --> Core["core service modules"]
    Repo --> Scripts["scripts"]
    Scripts --> Build["build-exe.ps1/cmd"]
    Scripts --> Installer["clean-n-adapt.iss"]
    Repo --> Docs["README.md / ARCHITECTURE.md"]
```

The repo keeps the public project name hyphenated, while the Python package uses `clean_n_adapt` because Python imports cannot use hyphens.
