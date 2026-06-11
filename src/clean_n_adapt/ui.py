from __future__ import annotations

import time

from rich.console import Console
from rich.table import Table

from .cleaner import human_size
from .models import AppEntry, ScanItem


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
    table.add_column("Publisher")
    table.add_column("Version")
    table.add_column("Uninstall", justify="center")
    for app in apps:
        table.add_row(app.name, app.publisher, app.version, "yes" if app.uninstall_string else "no")
    console.print(table)
