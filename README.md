# clean-n-adapt

Windows cleanup, app inventory, uninstall helper, and maintenance CLI.

I originally made this out of convenience as a small cache-clearing script when I was in 7th standard. I kept improving it over time as my own PCs changed, and now it has grown into a proper CLI tool with a database, safety checks, custom cleanup rules, app scans, reports, and a basic Rich menu UI.

## Tags

`windows` `python` `cli` `rich` `sqlite` `cleanup` `maintenance` `pyinstaller` `system-utility`

## Built with

- Python
- Rich
- SQLite
- PyInstaller
- PowerShell and CMD install scripts

## Install

Run the installer from an elevated terminal because it writes to `C:\Program Files`.

PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install.ps1
```

CMD:

```bat
scripts\install.cmd
```

Installed files:

```text
C:\Program Files\cleanNadapt\
```

State/database folder:

```text
C:\Program Files\cleanNadapt\.state\
```

After install, open a new terminal:

```powershell
cna
```

If `cna` is not found after install, add the install folder to PATH again:

PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\add-to-path.ps1
```

CMD:

```bat
scripts\add-to-path.cmd
```

## Build only

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

## Main usage

Dashboard:

```powershell
cna
cna status
cna status --compact
cna status --json
```

Rich menu UI:

```powershell
cna ui
```

Scan cache locations:

```powershell
cna scan --refresh
cna scan --refresh --include-admin
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

Dry-run cleanup:

```powershell
cna clean quick --dry-run
cna clean full --dry-run
```

Old compatibility command:

```powershell
cna cache clear
cna cache clear --dry-run
```

## Custom locations

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

App data is scanned once and stored in SQLite. Listing uses the cached DB. Run refresh when you want updated inventory.

```powershell
cna apps scan --refresh
cna apps list
cna apps list --refresh
cna apps list --query brave
cna apps uninstall "Brave"
cna apps uninstall "Brave" --dry-run
```

Apps are grouped/sorted by:

- system apps
- user apps
- Windows Store style packages

Uninstalling only launches official uninstall commands. If Windows does not provide one, clean-n-adapt refuses manual deletion.

## Boost, monitor, history, reports

```powershell
cna boost --dns
cna boost --store
cna boost --disk-cleanup
cna boost --high-performance
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

## Supported commands

```text
cna
cna ui
cna status [--compact] [--json] [--ttl-hours N]
cna scan [--refresh] [--clear-db] [--include-admin] [--min-age-hours N]
cna clean quick|deep|safe|browser|dev|gaming|windows|custom|full
cna cache clear
cna custom add|list|show|edit|enable|disable|remove|preview|clean
cna apps scan|list|uninstall
cna boost [--dns] [--store] [--disk-cleanup] [--high-performance] [--startup] [--all]
cna monitor [--interval N] [--count N] [--compact] [--json] [--refresh-each]
cna history [--limit N]
cna report --format txt|json
cna settings list|set
```

## Safety

- Blocks dangerous roots like `C:\`, `C:\Windows`, `C:\Program Files`, and the user profile root.
- Important-looking custom paths require `--advanced`.
- Locked files are skipped.
- Admin paths are skipped unless the shell is elevated.
- Custom cleanup never runs silently.
- App uninstall uses official uninstall commands only.

## Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md).
