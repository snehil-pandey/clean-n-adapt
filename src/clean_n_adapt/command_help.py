from __future__ import annotations

from rich.console import Console
from rich.panel import Panel
from rich.table import Table

from . import __version__


console = Console()


COMMANDS = [
    ("cna", "Open the interactive Clean-n-Adapt dashboard."),
    ("cna ui", "Open the Rich menu UI. Menus clear the previous screen before drawing."),
    ("cna tui", "Alias for the interactive dashboard."),
    ("cna status", "Show dashboard status: admin state, DB path, disk, memory, and indexed cleanup."),
    ("cna status --compact", "Print one-line status for quick checks or scripts."),
    ("cna status --json", "Print machine-readable status JSON."),
    ("cna doctor", "Analyze current PC state and print rule-based recommendations."),
    ("cna health", "Show an offline PC health score with category scores."),
    ("cna startup", "List startup entries with estimated impact."),
    ("cna startup disable NAME", "Disable one matched startup entry; requires --yes to apply."),
    ("cna storage top [PATH]", "Show largest folders under a path and record storage history."),
    ("cna storage history", "Show recorded free-space history samples."),
    ("cna browsers", "Show browser cache usage from the current cache index."),
    ("cna downloads audit", "Analyze old installers, archives, images, and duplicate names in Downloads."),
    ("cna duplicates scan PATH", "Find duplicate files by size and SHA-256 hash. Preview only in v1.1."),
    ("cna snapshot create", "Save apps/startup/cache snapshot for later comparison."),
    ("cna snapshot compare", "Compare the two latest snapshots."),
    ("cna schedule weekly|monthly", "Create a Windows Task Scheduler maintenance task."),
    ("cna restore-point", "Request a Windows restore point before risky maintenance."),
    ("cna permissions", "Check admin, registry, PATH, scheduler, restore point, and terminal integration."),
    ("cna permissions repair", "Show repair guidance for installer-owned integrations."),
    ("cna scan --refresh", "Refresh the cache/temp index using built-in known locations and bounded deep discovery."),
    ("cna scan --refresh --include-admin", "Also include admin-only locations such as Windows temp/update caches."),
    ("cna clean quick", "Clean safe indexed cache locations using the latest DB index."),
    ("cna clean quick --dry-run", "Preview quick clean without deleting anything."),
    ("cna clean deep", "Preview/clean deeper categories, including admin-aware targets when elevated."),
    ("cna clean browser", "Clean indexed browser caches only."),
    ("cna clean dev", "Clean indexed developer caches only."),
    ("cna clean gaming", "Clean indexed game/shader/launcher caches only."),
    ("cna clean windows", "Clean indexed Windows cache/temp targets."),
    ("cna clean custom", "Clean enabled custom rules. Always previews and asks first."),
    ("cna clean full", "Clean all built-in modes and then custom rules."),
    ("cna cache clear", "Compatibility alias for safe clean."),
    ("cna custom add PATH", "Add a custom cleanup rule after validation and preview."),
    ("cna custom list", "List saved custom rules."),
    ("cna custom preview [ID]", "Preview one custom rule or all enabled rules."),
    ("cna custom clean [ID]", "Clean one custom rule or all enabled rules; always confirms."),
    ("cna custom enable|disable ID", "Toggle a custom rule without deleting it."),
    ("cna custom edit ID", "Edit custom rule fields."),
    ("cna custom remove ID", "Remove the rule from DB without deleting files."),
    ("cna apps scan --refresh", "Refresh cached app inventory from registry and Store-style package keys."),
    ("cna apps list", "List cached app inventory, sorted by system/user/store type."),
    ("cna apps list --query NAME", "Search cached app inventory."),
    ("cna apps uninstall", "Open interactive app selection and launch official uninstallers only."),
    ("cna apps uninstall NAME", "Launch one app's official uninstaller only; refuses manual deletion."),
    ("cna boost --dns", "Run ipconfig /flushdns."),
    ("cna boost --store", "Run Windows Store cache reset."),
    ("cna boost --disk-cleanup", "Run Windows Disk Cleanup."),
    ("cna boost --high-performance", "Switch power plan to high performance."),
    ("cna boost --startup", "List startup registry entries only; does not disable them."),
    ("cna boost --all", "Run safe boost set: DNS, Store reset, Disk Cleanup."),
    ("cna monitor", "Watch status repeatedly."),
    ("cna monitor --compact --interval 5 --count 6", "Print compact monitor lines every 5 seconds for 6 updates."),
    ("cna history", "Show recent scans, cleans, boost actions, settings changes, and reports."),
    ("cna report --format txt", "Export a text report."),
    ("cna report --format json", "Export a JSON report."),
    ("cna settings list", "Show DB path and saved settings."),
    ("cna settings set KEY VALUE", "Save a setting, for example cache_warning_bytes."),
]


def print_home() -> None:
    console.rule("[bold cyan]clean-n-adapt")
    console.print(
        Panel(
            f"[bold]Version:[/bold] {__version__}\n"
            "[bold]Purpose:[/bold] Windows cleanup, app inventory, uninstall helper, boost actions, reports, and custom cache rules.\n"
            "[bold]Safety:[/bold] built-in cleanup uses known cache/temp locations; custom cleanup always previews first.",
            title="Home",
            border_style="cyan",
        )
    )
    quick = Table(title="Start Here")
    quick.add_column("Command", style="cyan")
    quick.add_column("Use")
    for command, use in [
        ("cna", "Open dashboard"),
        ("cna ui", "Open interactive menu"),
        ("cna doctor", "Get recommendations"),
        ("cna health", "Show PC health score"),
        ("cna scan --refresh", "Refresh cache index"),
        ("cna clean quick --dry-run", "Preview safe cleanup"),
        ("cna apps scan --refresh", "Refresh app inventory"),
        ("cna boost --startup", "Review startup entries"),
        ("cna help --all", "Show detailed command reference"),
    ]:
        quick.add_row(command, use)
    console.print(quick)


def print_command_reference(show_all: bool = False) -> None:
    table = Table(title="Command Reference")
    table.add_column("Command", style="cyan", no_wrap=True)
    table.add_column("Details")
    rows = COMMANDS if show_all else COMMANDS[:16]
    for command, details in rows:
        table.add_row(command, details)
    console.print(table)
    if not show_all:
        console.print("[yellow]Run cna help --all for every command.[/yellow]")
