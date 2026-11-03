# Widget and tray guide

`cna-widget.exe` is a small control surface for `cna.exe`. There is no main window: a card on the desktop and an icon in
the notification area. **Both exes must be in the same folder.**

## Starting and quitting

* Double-click `cna-widget.exe`. Only one copy runs; starting it again just shows the existing widget.
* The widget appears at the bottom-right of the work area the first time, then remembers where you put it.
* **X** in the card (or `Esc`) hides it to the tray. **Exit** in the tray menu quits.
* Tray menu -> **Start with Windows** adds/removes a `cna-widget` value under
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` (current user only).

## The card

    +--------------------------------------------+
    | [logo] Clean-n-Adapt              [pin] [x] |   header: drag here to move
    |        Auto-clean on . last run ...         |
    | +----------------------------------------+ |
    | | Windows (C:)        6.7 GB free of 252 GB| |   disk usage (red above 90 % used)
    | | [=================================   ]  | |
    | +----------------------------------------+ |
    | ( Quick | Browser | Dev | Gaming | Full )   |   cleaning profile
    | +----------------------------------------+ |
    | |  145.0 MB                      3 files  | |   result of the last action
    | |  can be reclaimed . Quick profile       | |
    | +----------------------------------------+ |
    | [ Scan ] [        Clean 145.0 MB         ] |   scan / contextual action
    |  status line, thin progress bar            |
    | [Duplicates] [Leftovers] [Quarantine]      |   one-click reports
    | [ Smart advice ]   [ Debloat ]             |   model advice / Windows debloat
    | +----------------------------------------+ |
    | | Results                       Details   | |   top rows of the last report
    | | user-temp                     145.0 MB  | |
    | +----------------------------------------+ |
    | Auto-clean in background            (o--)  |   Task Scheduler switch
    +--------------------------------------------+

| Control | What it does (the `cna` command it runs) |
|---|---|
| Profile chips | Select `quick`, `browser`, `dev`, `gaming` or `full`. Each profile keeps its most recent scan result while the widget stays open. The selected profile is remembered between runs. |
| **Scan** | `scan --profile <P> --json`. Fills the hero number and Results; arms the action button as **Clean <size>**. |
| Action button | The contextual, state-dependent button (below). Always asks you to **Confirm**. While a job runs it is **Stop**. |
| **Duplicates** | `dupes --min-size 10M --json` over Downloads/Documents/Desktop/Pictures/Videos/Music. Opens a full review list before any copy is staged for quarantine. |
| **Leftovers** | `remnants --json`. Opens a full review list before any candidate is staged for quarantine. |
| **Quarantine** | `quarantine list --json`: shows how much is held and the first few entries. |
| **Details** | Re-runs the last report in text form and opens it in a read-only Clean-n-Adapt report window. It has the same light/dark/accent treatment as the widget and a Copy action for sharing. |
| Auto-clean switch | `auto install --every-hours 6` / `auto remove`, then refreshes `auto status`. Uses the `cna.conf` in the state folder. |
| Pin | Keeps the card above other windows. Remembered. |

### Action button states

| State | Label | Enabled |
|---|---|---|
| Nothing to do yet | `Clean` | no |
| After Scan with reclaimable data | `Clean 145.0 MB` | yes |
| After Duplicates with sets found | `Quarantine extras` | yes |
| After Leftovers with candidates | `Quarantine leftovers` | yes |
| After Smart advice with suggestions | `Quarantine 50.0 MB` | yes |
| After Debloat with safe items pending | `Apply 10 safe tweaks` | yes |
| First click | `Confirm` (reverts after 5 s) | yes |
| A job is running | `Stop` | yes (terminates the running `cna.exe`) |

Running actions: Clean -> `clean --profile <P> --yes --json`; Quarantine extras -> `dupes --min-size 10M --quarantine --yes --json`;
The native review window lists every parsed candidate with path, category, size, confidence and reason. It supports Select all, Clear all, Inspect, and stages only checked eligible rows as repeated `--select PATH` arguments. The footer shows the selected eligible-item count and estimated size; protected rows remain visible but cannot be staged. Double-click a row to inspect its explanation.
Apply safe tweaks -> `debloat apply --preset safe --yes --json` (privacy, startup, services, disk; **never** Store apps).
The Smart advice tile runs `recommend --json`; the Debloat tile runs `debloat scan --json`.

## Tray icon

| Gesture | Result |
|---|---|
| Left click | Show / hide the widget |
| Right click | Menu: **Show/Hide widget**, **Quick clean now** (`clean --profile quick --yes`), **Run auto-clean now** (`auto run --force`), **Keep widget on top**, **Start with Windows**, **Review bloat apps...** (opens the reversible debloat scan), **Undo debloat changes...** (confirmation; `debloat undo --all --yes`), **Exit** |

When the widget is hidden (or for Quick clean), results appear as a tray notification, e.g. "Freed 145.0 MB".
If Explorer restarts, the tray icon is restored automatically. Windows may place new tray icons in the overflow
(^) area; drag the icon out to keep it visible.

## Look and feel

* The widget, review window, reports, and confirmation prompts follow the system **light/dark** mode and **accent colour**, switching live; rounded corners, thin border and shadow on Windows 11 (on older Windows the same card is drawn square).
* Per-monitor DPI aware; moving between monitors rescales.
* Segoe UI Variable / Segoe Fluent Icons when present, with fallbacks. A light layered translucency keeps the widget visually separate without reducing readability.

## Files and registry the widget touches

| What | Where |
|---|---|
| Settings | `widget.ini` in `%LOCALAPPDATA%\cna\` (or in `<exe folder>\.state\` in portable mode): `x`, `y`, `pinned`, `profile`, `visible` |
| Reports for "Details" | Kept in memory and shown in a themed read-only report window; no temporary Notepad file is created. |
| Autostart | `HKCU\...\Run\cna-widget` (only if enabled) |
| Read-only | `HKCU\...\Themes\Personalize\AppsUseLightTheme`, `HKCU\Software\Microsoft\Windows\DWM\AccentColor` |

It makes no network connections.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `cna-widget.exe --quit` | Asks a running widget to exit (used by the installer before replacing files). |
| "cna.exe not found next to the widget" | Put `cna.exe` in the same folder as `cna-widget.exe`. |
| Windows warns when starting the exe | The files are unsigned. "More info" -> "Run anyway", or build from source. |
| "N rule(s) need administrator and were skipped" | The `windows` rules (inside `Full`) need elevation. Start the widget with "Run as administrator" to include them. |
| Widget is off-screen after changing monitors | Delete `widget.ini` (or edit `x`/`y`) and restart the widget. |
| Tray icon missing | Check the overflow area; if Explorer was restarted the icon returns on its own. |
| Boxes instead of icons | The Segoe Fluent Icons / Segoe MDL2 Assets font is missing (not on a normal Windows 10/11). |
| "Stopped: memory cap reached" | The scan was too broad for the 125 MB budget; use a narrower profile, or run the CLI with narrower paths / a larger `--min-size`. |
| Auto-clean switch does not stay on | Creating the scheduled task failed (policy or permissions). Run `cna auto install` in a terminal to see the message. |

## Limitations

* One foreground action at a time; the card shows a single result (top three rows) - use **Details** for the full report.
* Duplicates in the widget use a fixed 10 MB minimum and the default folders; use the CLI for other folders or sizes.
* No Mica/Acrylic backdrop and no screen-reader integration (custom-drawn controls).
* Not yet verified on real Windows (it was exercised under Wine); see `TESTING.md`.
