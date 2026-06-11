from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path

from rich.console import Console
from rich.prompt import Confirm, IntPrompt, Prompt
from rich.table import Table

from . import __version__
from .apps import find_apps, installed_apps, uninstall_app
from .boost import disk_cleanup, flush_dns, reset_store, set_power_plan_high_performance
from .cleaner import clean_items, human_size, path_size, scan_targets
from .command_help import print_command_reference, print_home
from .custom_rules import (
    CATEGORIES,
    RISKS,
    RULE_TYPES,
    clean_rule,
    delete_rule,
    get_rule,
    list_rules,
    preview_rule,
    preview_rules,
    save_rule,
    set_rule_enabled,
    validate_rule,
)
from .diagnostics import (
    audit_downloads,
    browser_cache_items,
    build_recommendations,
    create_restore_point,
    create_schedule,
    create_snapshot,
    disable_startup_entry,
    find_duplicates,
    health_score,
    read_storage_history,
    record_storage_history,
    snapshot_diff,
    top_folders,
)
from .db import (
    add_cleanup_result,
    add_history,
    all_settings,
    clear_db,
    db_path,
    get_setting,
    history_rows,
    latest_snapshots,
    load_app_inventory,
    load_scan,
    save_app_inventory,
    save_scan,
    set_setting,
)
from .models import CustomRule, ScanItem, Target
from .monitor import print_snapshot, snapshot, snapshot_dict
from .reports import export_json, export_txt
from .startup import list_startup_entries
from .system import env_path, is_admin
from .targets import build_targets
from .ui import print_apps, print_custom_rules, print_history, print_menu, print_scan, print_startup


console = Console()

MODE_FILTERS = {
    "quick": {"Temp", "Thumbnails", "Windows", "Browsers", "App Cache"},
    "safe": {"Temp", "Thumbnails", "Windows", "Browsers", "App Cache"},
    "browser": {"Browsers"},
    "dev": {"Dev"},
    "gaming": {"Game"},
    "windows": {"Windows", "System", "Thumbnails"},
    "deep": {"Temp", "Thumbnails", "Windows", "Browsers", "App Cache", "Dev", "Game", "System"},
    "full": {"Temp", "Thumbnails", "Windows", "Browsers", "App Cache", "Dev", "Game", "System"},
}


def choose_indices(total: int, prompt: str = "Select numbers, a for all, or Enter to cancel") -> set[int]:
    if total <= 0:
        return set()
    raw = Prompt.ask(prompt, default="a").strip().casefold()
    if raw in {"a", "all"}:
        return set(range(total))
    if not raw:
        return set()
    selected: set[int] = set()
    for part in raw.replace(";", ",").split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            left, right = part.split("-", 1)
            if left.strip().isdigit() and right.strip().isdigit():
                start = max(1, int(left))
                end = min(total, int(right))
                selected.update(range(start - 1, end))
            continue
        if part.isdigit():
            index = int(part)
            if 1 <= index <= total:
                selected.add(index - 1)
    return selected


def select_scan_items(items: list[ScanItem], title: str) -> list[ScanItem]:
    table = Table(title=title)
    table.add_column("#", justify="right", style="cyan")
    table.add_column("Target")
    table.add_column("Category")
    table.add_column("Size", justify="right", style="green")
    table.add_column("Path", overflow="fold")
    for index, item in enumerate(items, 1):
        table.add_row(str(index), item.name, item.category, human_size(item.bytes_total), str(item.path))
    console.print(table)
    selected = choose_indices(len(items))
    return [item for index, item in enumerate(items) if index in selected]


def select_apps(apps: list, title: str = "Select apps") -> list:
    table = Table(title=title)
    table.add_column("#", justify="right", style="cyan")
    table.add_column("Name")
    table.add_column("Publisher")
    table.add_column("Type")
    table.add_column("Can uninstall", justify="center")
    for index, app in enumerate(apps, 1):
        table.add_row(
            str(index),
            app.name,
            app.publisher,
            app.app_kind,
            "yes" if app.uninstall_string or app.quiet_uninstall_string else "no",
        )
    console.print(table)
    selected = choose_indices(len(apps), "Select app numbers, a for all, or Enter to cancel")
    return [app for index, app in enumerate(apps) if index in selected]


def mode_items(mode: str, args: argparse.Namespace) -> list[ScanItem]:
    if mode == "custom":
        return []
    include_admin = bool(getattr(args, "include_admin", False) or mode in {"deep", "windows", "full"})
    if include_admin and not is_admin():
        console.print("[yellow]Admin-only locations are skipped because this shell is not elevated.[/yellow]")
        include_admin = False
    cache_ttl = getattr(args, "cache_ttl_hours", 24)
    items = [] if getattr(args, "refresh", False) else load_scan(max_age_hours=cache_ttl)
    if not items:
        scan_args = argparse.Namespace(include_admin=include_admin, min_age_hours=getattr(args, "min_age_hours", 12))
        console.print("[cyan]No fresh cache index found. Running scan once...[/cyan]")
        items = perform_scan(scan_args)
    allowed = MODE_FILTERS.get(mode, MODE_FILTERS["safe"])
    return [item for item in items if item.category in allowed]


def record_clean_history(mode: str, status: str, items: list[ScanItem], removed: int, failed: int) -> int:
    planned_bytes = sum(item.bytes_total for item in items)
    summary = f"{mode} clean: {removed} removed, {failed} failed"
    history_id = add_history("clean", status, summary, planned_bytes, removed, failed)
    for item in items:
        add_cleanup_result(history_id, mode, item.name, str(item.path), item.bytes_total, item.files, item.errors)
    return history_id


def cmd_dashboard(args: argparse.Namespace | None = None) -> int:
    ttl = None if args is None else getattr(args, "ttl_hours", None)
    snap = snapshot(max_age_hours=ttl)
    score = health_score()
    apps = load_app_inventory(max_age_hours=None)
    startup_count = len(list_startup_entries())
    console.rule("[bold cyan]clean-n-adapt dashboard")
    console.print(f"Version: [bold]{__version__}[/bold]")
    panel = Table(title="Clean-n-Adapt")
    panel.add_column("Metric")
    panel.add_column("Value", justify="right", style="green")
    panel.add_row("Health Score", f"{score.total}/100")
    panel.add_row("Junk Found", human_size(snap.indexed_bytes))
    panel.add_row("Apps Installed", str(len(apps)))
    panel.add_row("Startup Apps", str(startup_count))
    panel.add_row("Admin", "yes" if is_admin() else "no")
    console.print(panel)
    console.print(f"[cyan]Storage[/cyan]       {bar(score.storage)} {score.storage}/100")
    console.print(f"[cyan]Startup[/cyan]      {bar(score.startup)} {score.startup}/100")
    console.print(f"[cyan]Cache Hygiene[/cyan] {bar(score.cache)} {score.cache}/100")
    print_snapshot(snap)
    items = load_scan(max_age_hours=ttl)
    if items:
        print_scan(items[:10], "Top Indexed Cleanup")
    else:
        console.print("[yellow]No cache index yet. Run cna scan --refresh.[/yellow]")
    return 0


def bar(value: int, width: int = 18) -> str:
    value = max(0, min(100, value))
    filled = round(width * value / 100)
    return "#" * filled + "-" * (width - filled)


def cmd_help(args: argparse.Namespace) -> int:
    print_home()
    print_command_reference(show_all=args.all)
    return 0


def cmd_status(args: argparse.Namespace) -> int:
    snap = snapshot(max_age_hours=args.ttl_hours)
    if args.json:
        console.print(json.dumps(snapshot_dict(snap), indent=2))
        return 0
    if args.compact:
        print_snapshot(snap, compact=True)
        return 0
    return cmd_dashboard(args)


def perform_scan(args: argparse.Namespace) -> list[ScanItem]:
    if args.include_admin and not is_admin():
        console.print("[yellow]Admin locations requested, but this terminal is not elevated. They will be skipped.[/yellow]")
    include_admin = bool(args.include_admin and is_admin())
    min_age_seconds = max(0, int(args.min_age_hours * 3600))
    targets = build_targets(include_admin=include_admin)
    items = scan_targets(targets, min_age_seconds)
    save_scan(items)
    add_history("scan", "ok", f"indexed {len(items)} locations", sum(item.bytes_total for item in items), sum(item.files for item in items), sum(item.errors for item in items))
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


def clean_builtin_mode(args: argparse.Namespace, mode: str) -> int:
    items = mode_items(mode, args)
    if not items:
        console.print("[green]Nothing indexed for this clean mode.[/green]")
        return 0
    print_scan(items, f"{mode.title()} Clean Plan")
    total = sum(item.bytes_total for item in items)
    if args.dry_run:
        add_history("clean", "dry-run", f"{mode} clean dry-run: {human_size(total)} planned", total, sum(item.files for item in items), 0)
        console.print("[yellow]Dry run only. Nothing deleted.[/yellow]")
        return 0
    if not args.yes:
        selected = select_scan_items(items, f"Review {mode.title()} Cleanup")
        if not selected:
            add_history("clean", "cancelled", f"{mode} clean cancelled before selection", total, 0, 0)
            console.print("[yellow]Cancelled.[/yellow]")
            return 1
        items = selected
        total = sum(item.bytes_total for item in items)
    if not args.yes and not Confirm.ask(f"Delete {human_size(total)} from indexed {mode} locations?", default=False):
        add_history("clean", "cancelled", f"{mode} clean cancelled", total, 0, 0)
        console.print("[yellow]Cancelled.[/yellow]")
        return 1
    removed, failed, errors = clean_items(items, max(0, int(args.min_age_hours * 3600)))
    record_clean_history(mode, "ok" if failed == 0 else "partial", items, removed, failed)
    console.print(f"[green]Removed {removed} entries.[/green] [yellow]Skipped/failed: {failed}[/yellow]")
    for error in errors:
        console.print(f"[yellow]- {error}[/yellow]")
    console.print("[cyan]Run cna scan --refresh to update the database after cleanup.[/cyan]")
    return 0 if failed == 0 else 1


def cmd_clean(args: argparse.Namespace) -> int:
    mode = args.mode
    if mode == "custom":
        return cmd_custom_clean(argparse.Namespace(rule_id=None, all=True, dry_run=args.dry_run))
    code = clean_builtin_mode(args, mode)
    if mode == "full" and code == 0:
        custom_args = argparse.Namespace(rule_id=None, all=True, dry_run=args.dry_run)
        cmd_custom_clean(custom_args)
    return code


def cmd_cache_clear(args: argparse.Namespace) -> int:
    args.mode = "safe"
    return cmd_clean(args)


def build_rule_from_args(args: argparse.Namespace, existing: CustomRule | None = None) -> CustomRule:
    now = time.time()
    return CustomRule(
        id=None if existing is None else existing.id,
        name=args.name if args.name is not None else (existing.name if existing else Path(args.path).name),
        path=Path(args.path if args.path is not None else existing.path),
        rule_type=args.type if args.type is not None else (existing.rule_type if existing else "folder"),
        pattern=args.pattern if args.pattern is not None else (existing.pattern if existing else "*"),
        category=args.category if args.category is not None else (existing.category if existing else "Custom"),
        recursive=args.recursive if args.recursive is not None else (existing.recursive if existing else False),
        min_age_hours=args.min_age_hours if args.min_age_hours is not None else (existing.min_age_hours if existing else 12),
        min_size=args.min_size if args.min_size is not None else (existing.min_size if existing else 0),
        max_size=args.max_size if args.max_size is not None else (existing.max_size if existing else 0),
        include_patterns=args.include if args.include is not None else (existing.include_patterns if existing else ""),
        exclude_patterns=args.exclude if args.exclude is not None else (existing.exclude_patterns if existing else ""),
        risk=args.risk if args.risk is not None else (existing.risk if existing else "caution"),
        require_admin=args.require_admin if args.require_admin is not None else (existing.require_admin if existing else False),
        enabled=args.enabled if args.enabled is not None else (existing.enabled if existing else True),
        notes=args.notes if args.notes is not None else (existing.notes if existing else ""),
        advanced=args.advanced or (existing.advanced if existing else False),
        created_at=existing.created_at if existing else now,
        updated_at=now,
    )


def cmd_custom_add(args: argparse.Namespace) -> int:
    rule = build_rule_from_args(args)
    ok, errors, warnings = validate_rule(rule)
    for warning in warnings:
        console.print(f"[yellow]Warning:[/yellow] {warning}")
    if not ok:
        for error in errors:
            console.print(f"[red]Error:[/red] {error}")
        return 1
    preview = preview_rule(rule)
    print_scan([preview], "Preview Before Saving")
    if not args.yes and not Confirm.ask("Save this custom rule?", default=False):
        console.print("[yellow]Not saved.[/yellow]")
        return 1
    rule_id = save_rule(rule)
    add_history("custom", "ok", f"added custom rule #{rule_id}: {rule.name}", preview.bytes_total, preview.files, preview.errors)
    console.print(f"[green]Saved custom rule #{rule_id}.[/green]")
    return 0


def cmd_custom_list(_: argparse.Namespace) -> int:
    rules = list_rules(include_disabled=True)
    if rules:
        print_custom_rules(rules)
    else:
        console.print("[yellow]No custom rules yet.[/yellow]")
    return 0


def cmd_custom_show(args: argparse.Namespace) -> int:
    rule = get_rule(args.rule_id)
    if not rule:
        console.print("[red]Rule not found.[/red]")
        return 1
    print_custom_rules([rule])
    return 0


def cmd_custom_edit(args: argparse.Namespace) -> int:
    existing = get_rule(args.rule_id)
    if not existing:
        console.print("[red]Rule not found.[/red]")
        return 1
    rule = build_rule_from_args(args, existing)
    ok, errors, warnings = validate_rule(rule)
    for warning in warnings:
        console.print(f"[yellow]Warning:[/yellow] {warning}")
    if not ok:
        for error in errors:
            console.print(f"[red]Error:[/red] {error}")
        return 1
    preview = preview_rule(rule)
    print_scan([preview], "Preview Before Saving Edit")
    if not args.yes and not Confirm.ask("Save these changes?", default=False):
        console.print("[yellow]Not saved.[/yellow]")
        return 1
    save_rule(rule)
    add_history("custom", "ok", f"edited custom rule #{rule.id}: {rule.name}", preview.bytes_total, preview.files, preview.errors)
    console.print("[green]Rule updated.[/green]")
    return 0


def cmd_custom_enable(args: argparse.Namespace) -> int:
    ok = set_rule_enabled(args.rule_id, args.enabled)
    if not ok:
        console.print("[red]Rule not found.[/red]")
        return 1
    add_history("custom", "ok", f"{'enabled' if args.enabled else 'disabled'} custom rule #{args.rule_id}")
    console.print("[green]Updated.[/green]")
    return 0


def cmd_custom_remove(args: argparse.Namespace) -> int:
    rule = get_rule(args.rule_id)
    if not rule:
        console.print("[red]Rule not found.[/red]")
        return 1
    if not args.yes and not Confirm.ask(f"Remove custom rule #{args.rule_id} without deleting files?", default=False):
        console.print("[yellow]Cancelled.[/yellow]")
        return 1
    delete_rule(args.rule_id)
    add_history("custom", "ok", f"removed custom rule #{args.rule_id}: {rule.name}")
    console.print("[green]Rule removed.[/green]")
    return 0


def selected_custom_rules(args: argparse.Namespace) -> list[CustomRule]:
    if getattr(args, "rule_id", None):
        rule = get_rule(args.rule_id)
        return [] if rule is None else [rule]
    return list_rules(include_disabled=False)


def cmd_custom_preview(args: argparse.Namespace) -> int:
    rules = selected_custom_rules(args)
    if not rules:
        console.print("[yellow]No matching enabled custom rules.[/yellow]")
        return 0
    items = preview_rules(rules)
    print_scan(items, "Custom Rule Preview")
    add_history("custom", "preview", f"previewed {len(items)} custom rules", sum(item.bytes_total for item in items), sum(item.files for item in items), sum(item.errors for item in items))
    return 0


def cmd_custom_clean(args: argparse.Namespace) -> int:
    rules = [rule for rule in selected_custom_rules(args) if rule.enabled]
    if not rules:
        console.print("[yellow]No matching enabled custom rules.[/yellow]")
        return 1
    items = preview_rules(rules)
    print_scan(items, "Custom Clean Preview")
    total = sum(item.bytes_total for item in items)
    if args.dry_run:
        add_history("custom-clean", "dry-run", f"custom clean dry-run: {human_size(total)} planned", total, sum(item.files for item in items), sum(item.errors for item in items))
        console.print("[yellow]Dry run only. Nothing deleted.[/yellow]")
        return 0
    if not Confirm.ask(f"Custom cleanup always requires confirmation. Delete {human_size(total)}?", default=False):
        add_history("custom-clean", "cancelled", "custom clean cancelled", total, 0, 0)
        console.print("[yellow]Cancelled.[/yellow]")
        return 1
    removed = failed = bytes_total = 0
    all_errors: list[str] = []
    history_id = add_history("custom-clean", "running", "custom clean started", total, 0, 0)
    for rule, item in zip(rules, items):
        r_removed, r_failed, r_bytes, errors = clean_rule(rule)
        removed += r_removed
        failed += r_failed
        bytes_total += r_bytes
        all_errors.extend(errors)
        add_cleanup_result(history_id, "custom", rule.name, str(rule.path), item.bytes_total, item.files, r_failed)
    add_history("custom-clean", "ok" if failed == 0 else "partial", f"custom clean: {removed} removed, {failed} failed", bytes_total, removed, failed)
    console.print(f"[green]Removed {removed} custom entries.[/green] [yellow]Skipped/failed: {failed}[/yellow]")
    for error in all_errors[:12]:
        console.print(f"[yellow]- {error}[/yellow]")
    return 0 if failed == 0 else 1


def leftover_preview(app_name: str) -> list[ScanItem]:
    local = env_path("LOCALAPPDATA") or Path.home()
    roaming = env_path("APPDATA") or Path.home()
    program_data = env_path("ProgramData") or Path("C:/ProgramData")
    token = app_name.split()[0].casefold()
    items: list[ScanItem] = []
    for root in [local, roaming, program_data]:
        try:
            children = list(root.iterdir())
        except OSError:
            continue
        for child in children:
            if token and token in child.name.casefold():
                files, dirs, size, errors = path_size(child)
                if files or dirs:
                    items.append(ScanItem(child.name, "App Leftover Preview", child, "*", files, dirs, size, time.time(), False, errors))
    return items


def cmd_apps_list(args: argparse.Namespace) -> int:
    apps = [] if getattr(args, "refresh", False) else load_app_inventory(max_age_hours=getattr(args, "ttl_hours", None))
    if not apps:
        console.print("[cyan]No app inventory found. Running app scan once...[/cyan]")
        apps = installed_apps()
        save_app_inventory(apps)
        add_history("apps", "ok", f"indexed {len(apps)} apps")
    if args.query:
        needle = args.query.casefold()
        apps = [app for app in apps if needle in app.name.casefold()]
    if args.limit:
        apps = apps[: args.limit]
    if apps:
        print_apps(apps)
    else:
        console.print("[yellow]No matching apps found.[/yellow]")
    return 0


def cmd_apps_scan(args: argparse.Namespace) -> int:
    apps = installed_apps()
    save_app_inventory(apps)
    add_history("apps", "ok", f"refreshed app inventory: {len(apps)} apps")
    console.print(f"[green]Indexed {len(apps)} apps.[/green]")
    if not args.quiet:
        shown = apps[: args.limit] if args.limit else apps
        print_apps(shown)
    return 0


def cmd_apps_uninstall(args: argparse.Namespace) -> int:
    cached = load_app_inventory(max_age_hours=None)
    if not args.name:
        apps = cached or installed_apps()
        if not cached:
            save_app_inventory(apps)
        removable = [app for app in apps if app.uninstall_string or app.quiet_uninstall_string]
        selected = select_apps(removable[: args.limit] if args.limit else removable, "Select Apps To Uninstall")
        if not selected:
            console.print("[yellow]Cancelled.[/yellow]")
            return 1
        console.print("[bold]Selected:[/bold]")
        for app in selected:
            console.print(f"- {app.name}")
        if args.dry_run:
            add_history("uninstall", "dry-run", f"would launch uninstallers for {len(selected)} apps")
            console.print("[yellow]Dry run only. Nothing launched.[/yellow]")
            return 0
        if not args.yes and not Confirm.ask("Launch official uninstallers for selected apps?", default=False):
            add_history("uninstall", "cancelled", "cancelled interactive uninstall")
            return 1
        failures = 0
        for app in selected:
            proc = uninstall_app(app, quiet=args.quiet)
            failures += 0 if proc.returncode == 0 else 1
            add_history("uninstall", "ok" if proc.returncode == 0 else "failed", f"{app.name} uninstaller exited {proc.returncode}")
            console.print(f"[cyan]{app.name}[/cyan] uninstaller exited {proc.returncode}")
        return 0 if failures == 0 else 1
    matches = [app for app in cached if args.name.casefold() in app.name.casefold()] if cached else find_apps(args.name)
    if not matches:
        console.print("[red]No matching installed app found.[/red]")
        add_history("uninstall", "failed", f"no app matched {args.name}")
        return 1
    if len(matches) > 1 and not args.exact:
        console.print("[yellow]Multiple apps matched. Use a more exact name:[/yellow]")
        print_apps(matches[:20])
        return 1
    app = matches[0]
    if not app.uninstall_string and not app.quiet_uninstall_string:
        console.print("[red]This app has no official uninstall command. Refusing to delete it manually.[/red]")
        add_history("uninstall", "refused", f"{app.name} has no uninstall command")
        return 1
    print_apps([app])
    if args.dry_run:
        add_history("uninstall", "dry-run", f"would launch uninstaller for {app.name}")
        console.print("[yellow]Dry run only. Nothing launched.[/yellow]")
        return 0
    if not args.yes and not Confirm.ask("Launch the app's official uninstaller?", default=False):
        add_history("uninstall", "cancelled", f"cancelled uninstall for {app.name}")
        console.print("[yellow]Cancelled.[/yellow]")
        return 1
    proc = uninstall_app(app, quiet=args.quiet)
    add_history("uninstall", "ok" if proc.returncode == 0 else "failed", f"{app.name} uninstaller exited {proc.returncode}")
    console.print(f"[cyan]Uninstaller exited with code {proc.returncode}.[/cyan]")
    leftovers = leftover_preview(app.name)
    if leftovers:
        console.print("[yellow]Known-location leftover preview only. Nothing below was deleted.[/yellow]")
        print_scan(leftovers, "Leftover Preview")
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
    add_history("boost", "ok" if code == 0 else "failed", label)
    return code


def cmd_boost(args: argparse.Namespace) -> int:
    if args.all:
        args.dns = args.store = args.disk_cleanup = True
    if args.startup:
        entries = list_startup_entries()
        if entries:
            print_startup(entries)
        else:
            console.print("[green]No registry startup entries found.[/green]")
        add_history("boost", "preview", f"listed {len(entries)} startup entries")
    if not any([args.dns, args.store, args.disk_cleanup, args.high_performance]):
        if args.startup:
            return 0
        console.print("[yellow]Choose at least one boost action, --startup, or --all.[/yellow]")
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
    threshold = int(float(get_setting("cache_warning_bytes", "1073741824")))
    while loops is None or current < loops:
        if args.refresh_each:
            perform_scan(args)
        snap = snapshot(max_age_hours=args.ttl_hours)
        if args.json:
            console.print(json.dumps(snapshot_dict(snap), indent=2))
        else:
            print_snapshot(snap, compact=args.compact)
            if snap.indexed_bytes >= threshold:
                console.print(f"[yellow]Cache warning: indexed cleanup is {human_size(snap.indexed_bytes)}.[/yellow]")
            if not args.compact:
                items = load_scan(max_age_hours=args.ttl_hours)
                by_category: dict[str, int] = {}
                for item in items:
                    by_category[item.category] = by_category.get(item.category, 0) + item.bytes_total
                for category, bytes_total in sorted(by_category.items(), key=lambda row: row[1], reverse=True)[:5]:
                    console.print(f"[cyan]{category}[/cyan]: {human_size(bytes_total)}")
        current += 1
        if loops is not None and current >= loops:
            break
        time.sleep(max(1, args.interval))
    return 0


def cmd_history(args: argparse.Namespace) -> int:
    rows = history_rows(args.limit)
    if rows:
        print_history(rows)
    else:
        console.print("[yellow]No history yet.[/yellow]")
    return 0


def cmd_report(args: argparse.Namespace) -> int:
    output = args.output
    if output is None:
        output = "clean-n-adapt-report.json" if args.format == "json" else "clean-n-adapt-report.txt"
    path = Path(output)
    if args.format == "json":
        export_json(path)
    else:
        export_txt(path)
    add_history("report", "ok", f"exported {args.format} report to {path}")
    console.print(f"[green]Report written to {path}.[/green]")
    return 0


def cmd_settings_list(_: argparse.Namespace) -> int:
    settings = all_settings()
    console.print(f"[bold]DB:[/bold] {db_path()}")
    if not settings:
        console.print("[yellow]No custom settings saved.[/yellow]")
        return 0
    for key, value in settings.items():
        console.print(f"[cyan]{key}[/cyan] = {value}")
    return 0


def cmd_settings_set(args: argparse.Namespace) -> int:
    set_setting(args.key, args.value)
    add_history("settings", "ok", f"set {args.key}")
    console.print("[green]Setting saved.[/green]")
    return 0


def cmd_doctor(_: argparse.Namespace) -> int:
    score = health_score()
    console.rule("[bold cyan]clean-n-adapt doctor")
    console.print(f"[bold]PC Health Score:[/bold] {score.total}/100")
    if not score.recommendations:
        console.print("[green]No major maintenance issues found from the current index.[/green]")
        return 0
    console.print("\n[bold]Recommendations[/bold]")
    for rec in score.recommendations:
        color = "red" if rec.severity == "high" else "yellow" if rec.severity == "medium" else "cyan"
        console.print(f"[{color}]• {rec.title}[/{color}] - {rec.detail}")
    console.print("\n[bold]Recommended actions[/bold]")
    for index, rec in enumerate(score.recommendations, 1):
        console.print(f"{index}. [cyan]{rec.command}[/cyan]")
    add_history("doctor", "ok", f"doctor generated {len(score.recommendations)} recommendations")
    return 0


def cmd_health(_: argparse.Namespace) -> int:
    score = health_score()
    console.rule("[bold cyan]PC Health")
    console.print(f"[bold]PC Health Score:[/bold] {score.total}/100\n")
    for label, value in [
        ("Storage", score.storage),
        ("Startup", score.startup),
        ("Cache Hygiene", score.cache),
        ("Maintenance", score.maintenance),
        ("Apps", score.apps),
    ]:
        color = "green" if value >= 80 else "yellow" if value >= 60 else "red"
        console.print(f"{label:15} [{color}]{value}/100[/{color}]")
    if score.recommendations:
        console.print("\n[bold]Top advice:[/bold]")
        for rec in score.recommendations[:5]:
            console.print(f"- {rec.title}: [cyan]{rec.command}[/cyan]")
    add_history("health", "ok", f"health score {score.total}/100")
    return 0


def cmd_startup(args: argparse.Namespace) -> int:
    if args.startup_action == "disable":
        ok, message = disable_startup_entry(args.name, yes=args.yes)
        console.print(f"[{'green' if ok else 'yellow'}]{message}[/]")
        if not args.yes and not ok:
            console.print("[cyan]Run again with --yes to actually disable the exact matched entry.[/cyan]")
        return 0 if ok or not args.yes else 1
    entries = list_startup_entries()
    if entries:
        print_startup(entries)
    else:
        console.print("[green]No registry startup entries found.[/green]")
    add_history("startup", "preview", f"listed {len(entries)} startup entries")
    return 0


def cmd_storage_top(args: argparse.Namespace) -> int:
    root = Path(args.path).expanduser()
    rows = top_folders(root, limit=args.limit, depth=args.depth)
    if not rows:
        console.print("[yellow]No folders found or path is not readable.[/yellow]")
        return 1
    from rich.table import Table

    table = Table(title=f"Largest folders under {root}")
    table.add_column("Folder", overflow="fold")
    table.add_column("Size", justify="right", style="green")
    table.add_column("Files", justify="right")
    table.add_column("Errors", justify="right")
    for row in rows:
        table.add_row(str(row.path), human_size(row.bytes_total), str(row.files), str(row.errors))
    console.print(table)
    record_storage_history(root)
    add_history("storage", "ok", f"top folders under {root}")
    return 0


def cmd_storage_history(args: argparse.Namespace) -> int:
    rows = read_storage_history(args.limit)
    if not rows:
        record_storage_history()
        rows = read_storage_history(args.limit)
    from rich.table import Table

    table = Table(title="Storage History")
    table.add_column("Root")
    table.add_column("Free", justify="right", style="green")
    table.add_column("Used", justify="right")
    table.add_column("When", justify="right")
    for row in rows:
        age_hours = max(0, int((time.time() - float(row[5])) / 3600))
        table.add_row(row[1], human_size(int(row[4])), human_size(int(row[3])), f"{age_hours}h ago")
    console.print(table)
    return 0


def cmd_browsers(_: argparse.Namespace) -> int:
    items = browser_cache_items(max_age_hours=None)
    if not items:
        console.print("[yellow]No browser cache data in the DB yet. Run cna scan --refresh.[/yellow]")
        return 0
    print_scan(items, "Browser Cache Usage")
    add_history("browsers", "ok", f"listed {len(items)} browser cache targets", sum(item.bytes_total for item in items))
    return 0


def cmd_downloads_audit(args: argparse.Namespace) -> int:
    audit = audit_downloads(Path(args.path).expanduser() if args.path else None, old_days=args.old_days)
    console.rule("[bold cyan]Downloads Audit")
    console.print(f"[bold]Path:[/bold] {audit.path}")
    console.print(f"[bold]Total size:[/bold] {human_size(audit.total_bytes)}")
    console.print(f"Old archives: [cyan]{audit.old_archives}[/cyan]")
    console.print(f"Old installers: [cyan]{audit.old_installers}[/cyan]")
    console.print(f"Old images: [cyan]{audit.old_images}[/cyan]")
    console.print(f"Duplicate filenames: [cyan]{audit.duplicate_names}[/cyan]")
    if audit.largest:
        console.print("\n[bold]Largest files[/bold]")
        for path in audit.largest:
            try:
                size = path.stat().st_size
            except OSError:
                size = 0
            console.print(f"- {human_size(size):>10}  {path}")
    add_history("downloads", "ok", f"audited downloads at {audit.path}", audit.total_bytes)
    return 0


def cmd_duplicates_scan(args: argparse.Namespace) -> int:
    root = Path(args.path).expanduser()
    console.print(f"[cyan]Scanning duplicates under {root}. This can take a while.[/cyan]")
    groups = find_duplicates(root, min_size=args.min_size, limit=args.limit)
    if not groups:
        console.print("[green]No duplicate groups found with the current minimum size.[/green]")
        return 0
    savings = sum(group.size * (len(group.files) - 1) for group in groups)
    console.print(f"[bold]Potential savings:[/bold] {human_size(savings)}")
    for group in groups:
        console.print(f"\n[bold]{human_size(group.size)}[/bold] x {len(group.files)} copies")
        for path in group.files:
            console.print(f"- {path}")
    console.print("\n[yellow]No files were deleted. Duplicate clean is intentionally not automated in v1.1.[/yellow]")
    add_history("duplicates", "preview", f"found {len(groups)} duplicate groups", savings)
    return 0


def cmd_snapshot_create(args: argparse.Namespace) -> int:
    snap_id = create_snapshot(args.name)
    add_history("snapshot", "ok", f"created snapshot #{snap_id}: {args.name}")
    console.print(f"[green]Snapshot #{snap_id} created.[/green]")
    return 0


def cmd_snapshot_compare(args: argparse.Namespace) -> int:
    rows = latest_snapshots(limit=2, name=args.name)
    if len(rows) < 2:
        console.print("[yellow]Need at least two snapshots to compare.[/yellow]")
        return 1
    diff = snapshot_diff(rows[1][2], rows[0][2])
    console.rule("[bold cyan]Snapshot Compare")
    for label, values in diff.items():
        console.print(f"[bold]{label.replace('_', ' ').title()}[/bold]")
        if values:
            for value in values[:30]:
                console.print(f"- {value}")
        else:
            console.print("[green]No changes.[/green]")
    add_history("snapshot", "ok", f"compared snapshots {rows[1][0]} and {rows[0][0]}")
    return 0


def cmd_schedule(args: argparse.Namespace) -> int:
    code, output = create_schedule(args.frequency, args.command)
    if code == 0:
        console.print("[green]Scheduled maintenance task created/updated.[/green]")
    else:
        console.print(f"[yellow]Task Scheduler returned {code}.[/yellow]")
    if output:
        console.print(output)
    return code


def cmd_restore_point(args: argparse.Namespace) -> int:
    if not is_admin():
        console.print("[yellow]Restore points usually require an elevated terminal.[/yellow]")
    if not args.yes and not Confirm.ask("Create a Windows restore point now?", default=False):
        console.print("[yellow]Cancelled.[/yellow]")
        return 1
    code, output = create_restore_point(args.description)
    if code == 0:
        console.print("[green]Restore point requested successfully.[/green]")
    else:
        console.print(f"[yellow]Restore point command returned {code}.[/yellow]")
    if output:
        console.print(output)
    return code


def permission_rows() -> list[tuple[str, bool, str]]:
    checks: list[tuple[str, bool, str]] = []
    checks.append(("Administrator", is_admin(), "Needed for protected Windows locations and some boost actions."))
    checks.append(("PATH launcher", bool(shutil.which("cna") or shutil.which("cna.cmd")), "Open a new terminal after install if missing."))
    checks.append(("Task Scheduler", shutil.which("schtasks") is not None, "Needed for cna schedule."))
    checks.append(("Power Plan Access", shutil.which("powercfg") is not None, "Needed for high-performance boost."))
    checks.append(("Restore Point Command", shutil.which("powershell") is not None, "Restore point still requires system protection/admin."))
    try:
        import winreg

        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall"):
            checks.append(("Registry Access", True, "Installed app inventory can read uninstall registry."))
    except OSError as exc:
        checks.append(("Registry Access", False, str(exc)))
    terminal = env_path("LOCALAPPDATA") / "Microsoft" / "Windows Terminal" if env_path("LOCALAPPDATA") else Path()
    checks.append(("Windows Terminal", terminal.exists(), "Profile fragment is installed by the installer when possible."))
    return checks


def cmd_permissions(args: argparse.Namespace) -> int:
    if args.permissions_action == "repair":
        console.print("[cyan]Repair is installer-owned. Run install.ps1 again from the release folder to recreate PATH, shortcuts, and terminal profile.[/cyan]")
        add_history("permissions", "preview", "shown repair instructions")
        return 0
    table = Table(title="Permissions")
    table.add_column("Check")
    table.add_column("Status", justify="center")
    table.add_column("Details")
    for label, ok, detail in permission_rows():
        table.add_row(label, "[green]OK[/green]" if ok else "[red]NO[/red]", detail)
    console.print(table)
    add_history("permissions", "ok", "checked permissions")
    return 0


def ui_loop(_: argparse.Namespace | None = None) -> int:
    first_run_wizard()
    choices = [
        "Dashboard",
        "Doctor",
        "Health Score",
        "Quick Clean",
        "Deep Clean",
        "Custom Locations",
        "Apps",
        "Boost",
        "Monitor",
        "History",
        "Settings",
        "Exit",
    ]
    def pause() -> None:
        Prompt.ask("Press Enter to continue", default="")

    while True:
        console.clear()
        print_menu("clean-n-adapt", choices)
        pick = IntPrompt.ask("Choose", default=1)
        if pick == 1:
            console.clear()
            cmd_dashboard()
            pause()
        elif pick == 2:
            console.clear()
            cmd_doctor(argparse.Namespace())
            pause()
        elif pick == 3:
            console.clear()
            cmd_health(argparse.Namespace())
            pause()
        elif pick == 4:
            console.clear()
            cmd_clean(argparse.Namespace(mode="quick", dry_run=True, yes=False, refresh=False, cache_ttl_hours=24, include_admin=False, min_age_hours=12))
            pause()
        elif pick == 5:
            console.clear()
            cmd_clean(argparse.Namespace(mode="deep", dry_run=True, yes=False, refresh=False, cache_ttl_hours=24, include_admin=True, min_age_hours=12))
            pause()
        elif pick == 6:
            console.clear()
            cmd_custom_list(argparse.Namespace())
            pause()
        elif pick == 7:
            console.clear()
            app_choices = ["List cached apps", "Refresh app scan", "Search cached apps", "Back"]
            print_menu("Apps", app_choices)
            app_pick = IntPrompt.ask("Choose", default=1)
            if app_pick == 1:
                console.clear()
                cmd_apps_list(argparse.Namespace(query=None, limit=30, refresh=False, ttl_hours=None))
                pause()
            elif app_pick == 2:
                console.clear()
                cmd_apps_scan(argparse.Namespace(refresh=True, quiet=False, limit=30))
                pause()
            elif app_pick == 3:
                query = Prompt.ask("Search app", default="")
                console.clear()
                cmd_apps_list(argparse.Namespace(query=query or None, limit=30, refresh=False, ttl_hours=None))
                pause()
        elif pick == 8:
            console.clear()
            cmd_boost(argparse.Namespace(all=False, dns=False, store=False, disk_cleanup=False, high_performance=False, startup=True))
            pause()
        elif pick == 9:
            console.clear()
            cmd_monitor(argparse.Namespace(interval=1, count=1, compact=False, json=False, ttl_hours=None, refresh_each=False, include_admin=False, min_age_hours=12))
            pause()
        elif pick == 10:
            console.clear()
            cmd_history(argparse.Namespace(limit=20))
            pause()
        elif pick == 11:
            console.clear()
            cmd_settings_list(argparse.Namespace())
            pause()
        elif pick == 12:
            return 0
        else:
            console.print("[yellow]Pick a listed number.[/yellow]")


def first_run_wizard() -> None:
    if get_setting("profile", ""):
        return
    console.clear()
    print_menu("Welcome to Clean-n-Adapt", ["Casual User", "Gamer", "Developer", "Power User"])
    pick = IntPrompt.ask("Choose cleanup profile", default=1)
    profiles = {
        1: ("casual", "12", str(1024**3)),
        2: ("gamer", "6", str(2 * 1024**3)),
        3: ("developer", "6", str(2 * 1024**3)),
        4: ("power", "1", str(4 * 1024**3)),
    }
    profile, min_age, warning = profiles.get(pick, profiles[1])
    set_setting("profile", profile)
    set_setting("default_min_age_hours", min_age)
    set_setting("cache_warning_bytes", warning)
    add_history("settings", "ok", f"first-run profile: {profile}")
    console.print(f"[green]Profile saved:[/green] {profile}")
    Prompt.ask("Press Enter to continue", default="")


def add_scan_flags(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--include-admin", action="store_true", help="include known admin-only cache locations")
    parser.add_argument("--min-age-hours", type=float, default=12, help="only index/delete items older than this many hours")


def add_custom_rule_args(parser: argparse.ArgumentParser, edit: bool = False) -> None:
    if edit:
        parser.add_argument("rule_id", type=int)
        parser.add_argument("--path")
    else:
        parser.add_argument("path")
    parser.add_argument("--name")
    parser.add_argument("--type", choices=sorted(RULE_TYPES), default=None if edit else "folder")
    parser.add_argument("--pattern", default=None if edit else "*")
    parser.add_argument("--category", choices=sorted(CATEGORIES), default=None if edit else "Custom")
    parser.add_argument("--recursive", action=argparse.BooleanOptionalAction, default=None if edit else False)
    parser.add_argument("--min-age-hours", type=float, default=None if edit else 12)
    parser.add_argument("--min-size", type=int, default=None if edit else 0)
    parser.add_argument("--max-size", type=int, default=None if edit else 0)
    parser.add_argument("--include")
    parser.add_argument("--exclude")
    parser.add_argument("--risk", choices=sorted(RISKS), default=None if edit else "caution")
    parser.add_argument("--require-admin", action=argparse.BooleanOptionalAction, default=None if edit else False)
    parser.add_argument("--enabled", action=argparse.BooleanOptionalAction, default=None if edit else True)
    parser.add_argument("--notes")
    parser.add_argument("--advanced", action="store_true")
    parser.add_argument("--yes", action="store_true")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="cna", description="Adaptive Windows cleanup and boost CLI.")
    parser.add_argument("--version", action="version", version=f"clean-n-adapt {__version__}")
    sub = parser.add_subparsers(dest="command")

    help_cmd = sub.add_parser("help", help="show clean-n-adapt command helper")
    help_cmd.add_argument("--all", action="store_true", help="show every command and what it does")
    help_cmd.set_defaults(func=cmd_help)

    status = sub.add_parser("status", help="show dashboard/status")
    status.add_argument("--compact", action="store_true")
    status.add_argument("--json", action="store_true")
    status.add_argument("--ttl-hours", type=float, default=None)
    status.set_defaults(func=cmd_status)

    sub.add_parser("ui", help="open the Rich menu UI").set_defaults(func=ui_loop)
    sub.add_parser("tui", help="open the interactive dashboard").set_defaults(func=ui_loop)

    scan = sub.add_parser("scan", help="scan known cache locations and update the SQLite index")
    scan.add_argument("--refresh", action="store_true")
    scan.add_argument("--clear-db", action="store_true")
    add_scan_flags(scan)
    scan.set_defaults(func=cmd_scan)

    clean = sub.add_parser("clean", help="clean by mode")
    clean.add_argument("mode", choices=["quick", "deep", "safe", "browser", "dev", "gaming", "windows", "custom", "full"])
    clean.add_argument("--refresh", action="store_true")
    clean.add_argument("--cache-ttl-hours", type=float, default=24)
    clean.add_argument("--dry-run", action="store_true")
    clean.add_argument("--yes", action="store_true")
    add_scan_flags(clean)
    clean.set_defaults(func=cmd_clean)

    cache = sub.add_parser("cache", help="compatibility cache commands")
    cache_sub = cache.add_subparsers(dest="cache_command")
    clear = cache_sub.add_parser("clear", help="alias for cna clean safe")
    clear.add_argument("--refresh", action="store_true")
    clear.add_argument("--cache-ttl-hours", type=float, default=24)
    clear.add_argument("--dry-run", action="store_true")
    clear.add_argument("--yes", action="store_true")
    add_scan_flags(clear)
    clear.set_defaults(func=cmd_cache_clear)

    custom = sub.add_parser("custom", help="custom location rules")
    custom_sub = custom.add_subparsers(dest="custom_command")
    custom_add = custom_sub.add_parser("add")
    add_custom_rule_args(custom_add)
    custom_add.set_defaults(func=cmd_custom_add)
    custom_sub.add_parser("list").set_defaults(func=cmd_custom_list)
    custom_show = custom_sub.add_parser("show")
    custom_show.add_argument("rule_id", type=int)
    custom_show.set_defaults(func=cmd_custom_show)
    custom_edit = custom_sub.add_parser("edit")
    add_custom_rule_args(custom_edit, edit=True)
    custom_edit.set_defaults(func=cmd_custom_edit)
    custom_enable = custom_sub.add_parser("enable")
    custom_enable.add_argument("rule_id", type=int)
    custom_enable.set_defaults(func=cmd_custom_enable, enabled=True)
    custom_disable = custom_sub.add_parser("disable")
    custom_disable.add_argument("rule_id", type=int)
    custom_disable.set_defaults(func=cmd_custom_enable, enabled=False)
    custom_remove = custom_sub.add_parser("remove")
    custom_remove.add_argument("rule_id", type=int)
    custom_remove.add_argument("--yes", action="store_true")
    custom_remove.set_defaults(func=cmd_custom_remove)
    custom_preview = custom_sub.add_parser("preview")
    custom_preview.add_argument("rule_id", type=int, nargs="?")
    custom_preview.set_defaults(func=cmd_custom_preview)
    custom_clean = custom_sub.add_parser("clean")
    custom_clean.add_argument("rule_id", type=int, nargs="?")
    custom_clean.add_argument("--dry-run", action="store_true")
    custom_clean.set_defaults(func=cmd_custom_clean)

    apps = sub.add_parser("apps", help="installed app inventory and careful uninstall")
    apps_sub = apps.add_subparsers(dest="apps_command")
    apps_scan = apps_sub.add_parser("scan", help="refresh cached app inventory")
    apps_scan.add_argument("--refresh", action="store_true", help="kept for readability; scan always refreshes")
    apps_scan.add_argument("--quiet", action="store_true")
    apps_scan.add_argument("--limit", type=int, default=30)
    apps_scan.set_defaults(func=cmd_apps_scan)
    apps_list = apps_sub.add_parser("list")
    apps_list.add_argument("--query")
    apps_list.add_argument("--limit", type=int, default=0)
    apps_list.add_argument("--refresh", action="store_true", help="refresh app inventory before listing")
    apps_list.add_argument("--ttl-hours", type=float, default=None, help="reuse app inventory only if newer than this")
    apps_list.set_defaults(func=cmd_apps_list)
    apps_uninstall = apps_sub.add_parser("uninstall")
    apps_uninstall.add_argument("name", nargs="?")
    apps_uninstall.add_argument("--exact", action="store_true")
    apps_uninstall.add_argument("--quiet", action="store_true")
    apps_uninstall.add_argument("--dry-run", action="store_true")
    apps_uninstall.add_argument("--yes", action="store_true")
    apps_uninstall.add_argument("--limit", type=int, default=80)
    apps_uninstall.set_defaults(func=cmd_apps_uninstall)

    boost = sub.add_parser("boost", help="run safe speed/maintenance actions")
    boost.add_argument("--all", action="store_true")
    boost.add_argument("--dns", action="store_true")
    boost.add_argument("--store", action="store_true")
    boost.add_argument("--disk-cleanup", action="store_true")
    boost.add_argument("--high-performance", action="store_true")
    boost.add_argument("--startup", action="store_true", help="list registry startup entries only")
    boost.set_defaults(func=cmd_boost)

    sub.add_parser("doctor", help="analyze the PC and print recommendations").set_defaults(func=cmd_doctor)
    sub.add_parser("health", help="show an offline PC health score").set_defaults(func=cmd_health)

    startup = sub.add_parser("startup", help="analyze or disable startup entries")
    startup_sub = startup.add_subparsers(dest="startup_action")
    startup_sub.add_parser("list", help="list startup entries with estimated impact").set_defaults(func=cmd_startup)
    startup_disable = startup_sub.add_parser("disable", help="disable one exact startup entry")
    startup_disable.add_argument("name")
    startup_disable.add_argument("--yes", action="store_true")
    startup_disable.set_defaults(func=cmd_startup)
    startup.set_defaults(func=cmd_startup, startup_action="list")

    storage = sub.add_parser("storage", help="storage explorer and history")
    storage_sub = storage.add_subparsers(dest="storage_command")
    storage_top = storage_sub.add_parser("top", help="show largest folders under a path")
    storage_top.add_argument("path", nargs="?", default=str(Path.home()))
    storage_top.add_argument("--limit", type=int, default=15)
    storage_top.add_argument("--depth", type=int, default=2)
    storage_top.set_defaults(func=cmd_storage_top)
    storage_history = storage_sub.add_parser("history", help="show recorded free-space history")
    storage_history.add_argument("--limit", type=int, default=24)
    storage_history.set_defaults(func=cmd_storage_history)

    sub.add_parser("browsers", help="show browser cache usage from the current index").set_defaults(func=cmd_browsers)

    downloads = sub.add_parser("downloads", help="Downloads folder analysis")
    downloads_sub = downloads.add_subparsers(dest="downloads_command")
    downloads_audit = downloads_sub.add_parser("audit", help="audit old installers/archives/duplicates in Downloads")
    downloads_audit.add_argument("path", nargs="?")
    downloads_audit.add_argument("--old-days", type=int, default=90)
    downloads_audit.set_defaults(func=cmd_downloads_audit)

    duplicates = sub.add_parser("duplicates", help="duplicate file finder")
    duplicates_sub = duplicates.add_subparsers(dest="duplicates_command")
    duplicates_scan = duplicates_sub.add_parser("scan", help="scan for duplicate files")
    duplicates_scan.add_argument("path")
    duplicates_scan.add_argument("--min-size", type=int, default=1024 * 1024)
    duplicates_scan.add_argument("--limit", type=int, default=30)
    duplicates_scan.set_defaults(func=cmd_duplicates_scan)

    snapshot_cmd = sub.add_parser("snapshot", help="create and compare system snapshots")
    snapshot_sub = snapshot_cmd.add_subparsers(dest="snapshot_command")
    snapshot_create = snapshot_sub.add_parser("create")
    snapshot_create.add_argument("--name", default="default")
    snapshot_create.set_defaults(func=cmd_snapshot_create)
    snapshot_compare = snapshot_sub.add_parser("compare")
    snapshot_compare.add_argument("--name", default="default")
    snapshot_compare.set_defaults(func=cmd_snapshot_compare)

    schedule = sub.add_parser("schedule", help="create Windows Task Scheduler maintenance jobs")
    schedule.add_argument("frequency", choices=["weekly", "monthly"])
    schedule.add_argument("--command", default="cna clean quick --yes")
    schedule.set_defaults(func=cmd_schedule)

    restore = sub.add_parser("restore-point", help="create a Windows restore point")
    restore.add_argument("--description", default="clean-n-adapt restore point")
    restore.add_argument("--yes", action="store_true")
    restore.set_defaults(func=cmd_restore_point)

    permissions = sub.add_parser("permissions", help="check admin and Windows integration permissions")
    permissions_sub = permissions.add_subparsers(dest="permissions_action")
    permissions_sub.add_parser("check").set_defaults(func=cmd_permissions)
    permissions_sub.add_parser("repair").set_defaults(func=cmd_permissions)
    permissions.set_defaults(func=cmd_permissions, permissions_action="check")

    monitor = sub.add_parser("monitor", help="watch status without cleaning anything")
    monitor.add_argument("--interval", type=int, default=10)
    monitor.add_argument("--count", type=int, default=0)
    monitor.add_argument("--compact", action="store_true")
    monitor.add_argument("--json", action="store_true")
    monitor.add_argument("--ttl-hours", type=float, default=None)
    monitor.add_argument("--refresh-each", action="store_true")
    add_scan_flags(monitor)
    monitor.set_defaults(func=cmd_monitor)

    history = sub.add_parser("history", help="show last actions")
    history.add_argument("--limit", type=int, default=25)
    history.set_defaults(func=cmd_history)

    report = sub.add_parser("report", help="export a report")
    report.add_argument("--format", choices=["txt", "json"], default="txt")
    report.add_argument("--output")
    report.set_defaults(func=cmd_report)

    settings = sub.add_parser("settings", help="settings commands")
    settings_sub = settings.add_subparsers(dest="settings_command")
    settings_sub.add_parser("list").set_defaults(func=cmd_settings_list)
    settings_set = settings_sub.add_parser("set")
    settings_set.add_argument("key")
    settings_set.add_argument("value")
    settings_set.set_defaults(func=cmd_settings_set)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args_list = sys.argv[1:] if argv is None else argv
    if not args_list:
        return ui_loop()
    args = parser.parse_args(args_list)
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
