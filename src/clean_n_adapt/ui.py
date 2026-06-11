from __future__ import annotations

import time

from rich.console import Console
from rich.panel import Panel
from rich.table import Table

from .cleaner import human_size
from .models import AppEntry, CustomRule, ScanItem
from .startup import StartupEntry


console = Console()


def print_scan(items: list[ScanItem], title: str = "Cache Index") -> None:
    table = Table(title=title)
    table.add_column("Category", style="cyan", no_wrap=True)
    table.add_column("Target")
    table.add_column("Files", justify="right")
    table.add_column("Folders", justify="right")
    table.add_column("Size", justify="right", style="green")
    table.add_column("Age", justify="right")
    table.add_column("Path", overflow="fold")
    now = time.time()
    for item in sorted(items, key=lambda row: row.bytes_total, reverse=True):
        age_minutes = max(0, int((now - item.scanned_at) / 60))
        table.add_row(
            item.category,
            item.name,
            str(item.files),
            str(item.dirs),
            human_size(item.bytes_total),
            f"{age_minutes}m",
            str(item.path),
        )
    console.print(table)
    total = sum(item.bytes_total for item in items)
    console.print(f"[bold]Indexed cleanup:[/bold] {human_size(total)} across {len(items)} locations")


def print_apps(apps: list[AppEntry]) -> None:
    table = Table(title="Installed Apps")
    table.add_column("Name")
    table.add_column("Scope")
    table.add_column("Type")
    table.add_column("Publisher")
    table.add_column("Version")
    table.add_column("Install Path", overflow="fold")
    table.add_column("Can uninstall cleanly", justify="center")
    for app in apps:
        table.add_row(
            app.name,
            app.install_scope,
            app.app_kind,
            app.publisher,
            app.version,
            app.install_location,
            "yes" if app.uninstall_string or app.quiet_uninstall_string else "no",
        )
    console.print(table)


def print_custom_rules(rules: list[CustomRule]) -> None:
    table = Table(title="Custom Locations")
    table.add_column("ID", justify="right")
    table.add_column("Name")
    table.add_column("Enabled", justify="center")
    table.add_column("Risk")
    table.add_column("Category")
    table.add_column("Type")
    table.add_column("Pattern")
    table.add_column("Path", overflow="fold")
    for rule in rules:
        style = "green" if rule.risk == "safe" else "yellow" if rule.risk == "caution" else "red"
        table.add_row(
            str(rule.id),
            rule.name,
            "yes" if rule.enabled else "no",
            f"[{style}]{rule.risk}[/{style}]",
            rule.category,
            rule.rule_type,
            rule.pattern,
            str(rule.path),
        )
    console.print(table)


def print_history(rows: list[tuple]) -> None:
    table = Table(title="History")
    table.add_column("ID", justify="right")
    table.add_column("Action")
    table.add_column("Status")
    table.add_column("Summary")
    table.add_column("Size", justify="right")
    table.add_column("Failures", justify="right")
    for row in rows:
        table.add_row(str(row[0]), row[1], row[2], row[3], human_size(int(row[4])), str(row[6]))
    console.print(table)


def print_startup(entries: list[StartupEntry]) -> None:
    table = Table(title="Startup Entries")
    table.add_column("Name")
    table.add_column("Location", style="cyan")
    table.add_column("Command", overflow="fold")
    for entry in entries:
        table.add_row(entry.name, entry.location, entry.command)
    console.print(table)


def print_menu(title: str, choices: list[str]) -> None:
    body = "\n".join(f"[cyan]{index}[/cyan]. {choice}" for index, choice in enumerate(choices, 1))
    console.print(Panel(body, title=title, border_style="cyan"))
