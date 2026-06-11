from __future__ import annotations

import argparse
import json
import sys
import time

from rich.console import Console
from rich.prompt import Confirm

from . import __version__
from .apps import find_apps, installed_apps, uninstall_app
from .boost import disk_cleanup, flush_dns, reset_store, set_power_plan_high_performance
from .cleaner import clean_items, human_size, scan_targets
from .db import clear_db, db_path, load_scan, save_scan
from .monitor import print_snapshot, snapshot, snapshot_dict
from .system import is_admin
from .targets import build_targets
from .ui import print_apps, print_scan


console = Console()


def cmd_status(args: argparse.Namespace) -> int:
    snap = snapshot(max_age_hours=args.ttl_hours)
    if args.json:
        console.print(json.dumps(snapshot_dict(snap), indent=2))
        return 0
    if args.compact:
        print_snapshot(snap, compact=True)
        return 0
    items = load_scan(max_age_hours=args.ttl_hours)
    console.rule("[bold cyan]clean-n-adapt")
    console.print(f"Version: [bold]{__version__}[/bold]")
    console.print(f"Admin: [bold]{'yes' if is_admin() else 'no'}[/bold]")
    console.print(f"Database: [bold]{db_path()}[/bold]")
    print_snapshot(snap)
    if items:
        print_scan(items, "Stored Cache Index")
    else:
        console.print("[yellow]No cache index yet. Run:[/yellow] cna scan --refresh")
    return 0


def perform_scan(args: argparse.Namespace) -> list:
    if args.include_admin and not is_admin():
        console.print("[yellow]Admin locations requested, but this terminal is not elevated. They will be skipped.[/yellow]")
    include_admin = bool(args.include_admin and is_admin())
    min_age_seconds = max(0, int(args.min_age_hours * 3600))
    targets = build_targets(include_admin=include_admin)
    items = scan_targets(targets, min_age_seconds)
    save_scan(items)
    return items


def cmd_scan(args: argparse.Namespace) -> int:
    if args.clear_db:
        clear_db()
        console.print("[green]Cleared stored cache index.[/green]")
    items = perform_scan(args)
    if items:
        print_scan(items, "Fresh Cache Scan")
    else:
        console.print("[green]No disposable cache/temp items found.[/green]")
    return 0


def load_or_scan(args: argparse.Namespace):
    items = [] if args.refresh else load_scan(max_age_hours=args.cache_ttl_hours)
    if not items:
        console.print("[cyan]No fresh cache index found. Running adaptive scan once...[/cyan]")
        items = perform_scan(args)
    return items


def cmd_cache_clear(args: argparse.Namespace) -> int:
    items = load_or_scan(args)
    if not items:
        console.print("[green]Nothing to clean.[/green]")
        return 0
    print_scan(items, "Cleanup Plan")
    total = sum(item.bytes_total for item in items)
    if args.dry_run:
        console.print("[yellow]Dry run only. Nothing deleted.[/yellow]")
        return 0
    if not args.yes and not Confirm.ask(f"Delete {human_size(total)} from indexed safe cache locations?", default=False):
        console.print("[yellow]Cancelled.[/yellow]")
        return 1
    removed, failed, errors = clean_items(items, max(0, int(args.min_age_hours * 3600)))
    console.print(f"[green]Removed {removed} entries.[/green] [yellow]Skipped/failed: {failed}[/yellow]")
    for error in errors:
        console.print(f"[yellow]- {error}[/yellow]")
    console.print("[cyan]Run cna scan --refresh to update the database after cleanup.[/cyan]")
    return 0


def cmd_apps_list(args: argparse.Namespace) -> int:
    apps = installed_apps() if not args.query else find_apps(args.query)
    if args.limit:
        apps = apps[: args.limit]
    if apps:
        print_apps(apps)
    else:
        console.print("[yellow]No matching apps found.[/yellow]")
    return 0


def cmd_apps_uninstall(args: argparse.Namespace) -> int:
    matches = find_apps(args.name)
    if not matches:
        console.print("[red]No matching installed app found.[/red]")
        return 1
    if len(matches) > 1 and not args.exact:
        console.print("[yellow]Multiple apps matched. Use a more exact name:[/yellow]")
        print_apps(matches[:20])
        return 1
    app = matches[0]
    if not app.uninstall_string and not app.quiet_uninstall_string:
        console.print("[red]This app has no official uninstall command. Refusing to delete it manually.[/red]")
        return 1
    console.print(f"[bold]App:[/bold] {app.name}")
    console.print(f"[bold]Publisher:[/bold] {app.publisher or 'unknown'}")
    console.print(f"[bold]Uninstall command registered:[/bold] {'yes' if app.uninstall_string else 'quiet only'}")
    if args.dry_run:
        console.print("[yellow]Dry run only. Nothing launched.[/yellow]")
        return 0
    if not args.yes and not Confirm.ask("Launch the app's official uninstaller?", default=False):
        console.print("[yellow]Cancelled.[/yellow]")
        return 1
    proc = uninstall_app(app, quiet=args.quiet)
    console.print(f"[cyan]Uninstaller exited with code {proc.returncode}.[/cyan]")
    return int(proc.returncode)


def run_boost_action(label: str, func) -> int:
    console.print(f"[cyan]{label}...[/cyan]")
    code, output = func()
    if code == 0:
        console.print(f"[green]{label} done.[/green]")
    else:
        console.print(f"[yellow]{label} returned code {code}.[/yellow]")
    if output:
        console.print(output)
    return code


def cmd_boost(args: argparse.Namespace) -> int:
    if args.all:
        args.dns = args.store = args.disk_cleanup = True
    if not any([args.dns, args.store, args.disk_cleanup, args.high_performance]):
        console.print("[yellow]Choose at least one boost action or pass --all.[/yellow]")
        return 1
    codes: list[int] = []
    if args.dns:
        codes.append(run_boost_action("Flush DNS", flush_dns))
    if args.store:
        codes.append(run_boost_action("Reset Windows Store cache", reset_store))
    if args.disk_cleanup:
        codes.append(run_boost_action("Run Disk Cleanup", disk_cleanup))
    if args.high_performance:
        codes.append(run_boost_action("Set high performance power plan", set_power_plan_high_performance))
    return 0 if all(code == 0 for code in codes) else 1


def cmd_monitor(args: argparse.Namespace) -> int:
    loops = args.count if args.count > 0 else None
    current = 0
    while loops is None or current < loops:
        if args.refresh_each:
            perform_scan(args)
        snap = snapshot(max_age_hours=args.ttl_hours)
        if args.json:
            console.print(json.dumps(snapshot_dict(snap), indent=2))
        else:
            print_snapshot(snap, compact=args.compact)
        current += 1
        if loops is not None and current >= loops:
            break
        time.sleep(max(1, args.interval))
    return 0


def add_scan_flags(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--include-admin", action="store_true", help="include known admin-only cache locations")
    parser.add_argument("--min-age-hours", type=float, default=12, help="only index/delete items older than this many hours")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="cna", description="Adaptive Windows cleanup and boost CLI.")
    parser.add_argument("--version", action="version", version=f"clean-n-adapt {__version__}")
    sub = parser.add_subparsers(dest="command")

    status = sub.add_parser("status", help="show database, cached scan, disk, and memory status")
    status.add_argument("--compact", action="store_true", help="print a one-line status")
    status.add_argument("--json", action="store_true", help="print machine-readable status")
    status.add_argument("--ttl-hours", type=float, default=None, help="only count cache index rows newer than this")
    status.set_defaults(func=cmd_status)

    scan = sub.add_parser("scan", help="scan known cache locations and update the SQLite index")
    scan.add_argument("--refresh", action="store_true", help="kept for readability; scan always refreshes")
    scan.add_argument("--clear-db", action="store_true", help="clear previous scan rows before scanning")
    add_scan_flags(scan)
    scan.set_defaults(func=cmd_scan)

    cache = sub.add_parser("cache", help="cache cleanup commands")
    cache_sub = cache.add_subparsers(dest="cache_command")
    clear = cache_sub.add_parser("clear", help="clear indexed cache/temp files")
    clear.add_argument("--refresh", action="store_true", help="force a fresh scan before cleaning")
    clear.add_argument("--cache-ttl-hours", type=float, default=24, help="reuse scan results newer than this")
    clear.add_argument("--dry-run", action="store_true", help="show plan without deleting")
    clear.add_argument("--yes", action="store_true", help="skip confirmation")
    add_scan_flags(clear)
    clear.set_defaults(func=cmd_cache_clear)

    apps = sub.add_parser("apps", help="installed app inventory and careful uninstall")
    apps_sub = apps.add_subparsers(dest="apps_command")
    apps_list = apps_sub.add_parser("list", help="list installed apps")
    apps_list.add_argument("--query", help="filter by app name")
    apps_list.add_argument("--limit", type=int, default=0, help="limit displayed rows")
    apps_list.set_defaults(func=cmd_apps_list)
    apps_uninstall = apps_sub.add_parser("uninstall", help="launch an app's official uninstaller")
    apps_uninstall.add_argument("name", help="app name or search term")
    apps_uninstall.add_argument("--exact", action="store_true", help="allow first match without ambiguity checks")
    apps_uninstall.add_argument("--quiet", action="store_true", help="use quiet uninstall string when registered")
    apps_uninstall.add_argument("--dry-run", action="store_true", help="show what would run")
    apps_uninstall.add_argument("--yes", action="store_true", help="skip confirmation")
    apps_uninstall.set_defaults(func=cmd_apps_uninstall)

    boost = sub.add_parser("boost", help="run safe speed/maintenance actions")
    boost.add_argument("--all", action="store_true", help="run safe boost actions")
    boost.add_argument("--dns", action="store_true", help="flush DNS cache")
    boost.add_argument("--store", action="store_true", help="reset Windows Store cache")
    boost.add_argument("--disk-cleanup", action="store_true", help="run Windows Disk Cleanup")
    boost.add_argument("--high-performance", action="store_true", help="switch to high performance power plan")
    boost.set_defaults(func=cmd_boost)

    monitor = sub.add_parser("monitor", help="watch status without cleaning anything")
    monitor.add_argument("--interval", type=int, default=10, help="seconds between updates")
    monitor.add_argument("--count", type=int, default=0, help="number of updates; 0 means keep running")
    monitor.add_argument("--compact", action="store_true", help="print one line per update")
    monitor.add_argument("--json", action="store_true", help="print JSON per update")
    monitor.add_argument("--ttl-hours", type=float, default=None, help="only count cache index rows newer than this")
    monitor.add_argument("--refresh-each", action="store_true", help="run a fresh scan before every update")
    add_scan_flags(monitor)
    monitor.set_defaults(func=cmd_monitor)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if not hasattr(args, "func"):
        parser.print_help()
        return 0
    try:
        return int(args.func(args))
    except KeyboardInterrupt:
        console.print("\n[yellow]Cancelled.[/yellow]")
        return 130
    except Exception as exc:
        console.print(f"[red]Error:[/red] {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
