# Architecture

clean-n-adapt is a Windows-first Python CLI. The CLI and the Rich menu UI call the same command functions, so scripted usage and interactive usage stay linked.

## High-level flow

```mermaid
flowchart TD
    User["User"] --> Entry["cna / cna.exe"]
    Entry --> Parser["argparse command router"]
    Parser --> CLI["CLI commands"]
    Parser --> TUI["Rich menu UI"]
    TUI --> CLI
    CLI --> Services["Scan, clean, apps, boost, monitor, reports"]
    Services --> DB["SQLite state DB"]
    Services --> Windows["Windows APIs / registry / filesystem"]
```

## Install and runtime layout

```mermaid
flowchart TD
    Source["Repo checkout"] --> Build["PyInstaller build"]
    Build --> Exe["dist/cna.exe"]
    Exe --> Install["Install script"]
    Install --> ProgramFiles["C:/Program Files/cleanNadapt"]
    ProgramFiles --> RuntimeExe["cna.exe"]
    ProgramFiles --> Launcher["cna.cmd"]
    ProgramFiles --> State[".state/clean-n-adapt.db"]
    Launcher --> RuntimeExe
    RuntimeExe --> State
```

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
    Targets --> FS["known temp/cache/browser/dev/game/windows paths"]
    FS --> ScanRows["scan rows"]
    ScanRows --> DB["SQLite scans table"]
    CleanCmd["cna clean mode"] --> DB
    DB --> Plan["cleanup plan table"]
    Plan --> Confirm["confirmation unless dry-run/yes"]
    Confirm --> Delete["delete known disposable entries"]
    Delete --> History["history + cleanup_results"]
```

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
