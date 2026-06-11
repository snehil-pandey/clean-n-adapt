# clean-n-adapt Architecture

clean-n-adapt is a Windows-first Python CLI. The CLI and the Rich menu UI call the same command functions, so scripted usage and interactive usage stay linked.

## Runtime Principles

- Keep CLI and TUI behavior connected through the same command functions.
- Keep installer/native Windows integration separate from the cleanup engine.
- Store runtime state beside the executable by default.
- Cache expensive scans in SQLite and refresh only when requested.
- Prefer previews and official uninstall commands over blind deletion.
- Keep deep discovery bounded and skip runtime/tool/virtual environment folders.

## High-level flow

```mermaid
flowchart TD
    User["User"] --> Entry["cna / cna.exe"]
    Entry --> Parser["argparse command router"]
    Parser --> CLI["CLI commands"]
    Parser --> TUI["Rich dashboard/menu UI"]
    TUI --> CLI
    CLI --> Services["Scan, clean, apps, boost, monitor, reports"]
    Services --> DB["SQLite state DB"]
    Services --> Windows["Windows APIs / registry / filesystem"]
```

## Layered target shape

```mermaid
flowchart TD
    CLI["CLI Layer"] --> Core["Core Engine"]
    TUI["TUI Layer"] --> Core
    Installer["Installer Layer"] --> Windows["Windows Integration"]
    Core --> Cleanup["Cleanup"]
    Core --> Apps["Apps"]
    Core --> Monitor["Monitor"]
    Core --> History["History"]
    Core --> Permissions["Permissions"]
    Core --> Health["Health"]
    Windows --> Shortcuts["Start Menu / Desktop"]
    Windows --> Path["PATH"]
    Windows --> Terminal["Windows Terminal profile"]
    Windows --> UAC["Admin manifest / UAC"]
```

## Install and runtime layout

```mermaid
flowchart TD
    Source["Repo checkout"] --> Build["PyInstaller build"]
    Build --> Exe["dist/cna.exe"]
    Exe --> Install["Install script"]
    Install --> AppFolder["Install folder"]
    AppFolder --> RuntimeExe["cna.exe"]
    AppFolder --> Launcher["cna.cmd"]
    AppFolder --> State[".state/clean-n-adapt.db"]
    Launcher --> RuntimeExe
    RuntimeExe --> State
```

## Source layout

```mermaid
flowchart TD
    Repo["clean-n-adapt repo"] --> Src["src/"]
    Src --> Package["clean_n_adapt package"]
    Package --> CLI["cli.py command router"]
    Package --> UI["ui.py Rich menus"]
    Package --> Services["apps, cleaner, boost, monitor, reports"]
    Package --> DBLayer["db.py SQLite access"]
```

The repository uses a hyphenated project name and an underscored Python package name because Python modules cannot be imported with hyphens.

## SQLite state

```mermaid
flowchart LR
    DB["clean-n-adapt.db"] --> Scans["scans"]
    DB --> Custom["custom_rules"]
    DB --> Apps["app_inventory"]
    DB --> Settings["settings"]
    DB --> History["history"]
    DB --> Results["cleanup_results"]
    Scans --> Clean["clean modes"]
    Custom --> CustomClean["custom preview/clean"]
    Apps --> AppList["apps list/search/uninstall"]
    Settings --> Monitor["status/monitor thresholds"]
    History --> Reports["txt/json reports"]
```

## Cache scan and clean

```mermaid
flowchart TD
    ScanCmd["cna scan --refresh"] --> Targets["built-in target discovery"]
    Targets --> Known["known temp/cache/browser/dev/game/windows paths"]
    Targets --> Deep["bounded deep discovery, depth 10"]
    Deep --> Skip["skip runtime/tool/venv/user-content folders"]
    Known --> FS["scan candidates"]
    Skip --> FS
    FS --> ScanRows["scan rows"]
    ScanRows --> DB["SQLite scans table"]
    CleanCmd["cna clean mode"] --> DB
    DB --> Plan["cleanup plan table"]
    Plan --> Confirm["confirmation unless dry-run/yes"]
    Confirm --> Delete["delete known disposable entries"]
    Delete --> History["history + cleanup_results"]
```

## Diagnostics and recommendations

```mermaid
flowchart TD
    Doctor["doctor / health"] --> Rules["offline rule engine"]
    Rules --> ScanDB["cache scan index"]
    Rules --> Startup["startup entries"]
    Rules --> Apps["app inventory"]
    Rules --> Disk["disk/free-space samples"]
    Rules --> Advice["ranked recommendations"]
    Advice --> Commands["safe command suggestions"]
```

The doctor command does not call an online service or LLM. It uses local thresholds and cached inventory to explain what the user can do next.

## Custom rules

```mermaid
flowchart TD
    Add["custom add/edit"] --> Validate["safety validation"]
    Validate --> Block["block dangerous roots"]
    Validate --> Warn["warn important-looking paths"]
    Warn --> Preview["mandatory preview"]
    Preview --> Save["save rule"]
    Save --> DB["custom_rules"]
    Clean["custom clean"] --> DB
    DB --> Preview2["mandatory preview"]
    Preview2 --> Confirm["confirmation every time"]
    Confirm --> Delete["delete matching files"]
    Delete --> History["history/results"]
```

## App inventory

```mermaid
flowchart TD
    Refresh["apps scan --refresh"] --> Registry["Windows uninstall registry"]
    Refresh --> Store["Windows Store package registry"]
    Registry --> Classify["system/user/desktop"]
    Store --> ClassifyStore["system/user/windows-store"]
    Classify --> AppDB["app_inventory"]
    ClassifyStore --> AppDB
    List["apps list/search"] --> AppDB
    Uninstall["apps uninstall"] --> AppDB
    Uninstall --> Official["official uninstall command only"]
```

## Monitor and reports

```mermaid
flowchart TD
    Monitor["status/monitor"] --> DB["SQLite"]
    Monitor --> Disk["disk usage"]
    Monitor --> Memory["memory usage"]
    DB --> Categories["indexed size/category totals"]
    Disk --> Output["Rich table / compact / JSON"]
    Memory --> Output
    Categories --> Output
    Report["report txt/json"] --> DB
    Report --> OutputFile["report file"]
```
