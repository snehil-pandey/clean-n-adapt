from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path

from .cleaner import human_size, path_size
from .db import (
    add_history,
    load_app_inventory,
    load_scan,
    save_snapshot,
    storage_history_rows,
    upsert_storage_sample,
)
from .startup import StartupEntry, list_startup_entries
from .system import is_admin


@dataclass
class Recommendation:
    severity: str
    title: str
    detail: str
    command: str


@dataclass
class HealthScore:
    total: int
    storage: int
    startup: int
    cache: int
    maintenance: int
    apps: int
    recommendations: list[Recommendation]


@dataclass
class FolderSize:
    path: Path
    bytes_total: int
    files: int
    dirs: int
    errors: int


@dataclass
class DuplicateGroup:
    size: int
    digest: str
    files: list[Path]


@dataclass
class DownloadsAudit:
    path: Path
    total_bytes: int
    old_archives: int
    old_installers: int
    old_images: int
    duplicate_names: int
    largest: list[Path]


BROWSER_NAMES = ("chrome", "edge", "brave", "firefox", "opera", "vivaldi")
ARCHIVE_SUFFIXES = {".zip", ".rar", ".7z", ".tar", ".gz", ".iso"}
INSTALLER_SUFFIXES = {".exe", ".msi", ".msix", ".appx"}
IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp", ".heic"}


def disk_percent_free(path: Path | None = None) -> float:
    usage = shutil.disk_usage(path or Path.home().anchor or "C:/")
    return usage.free / usage.total if usage.total else 0.0


def browser_cache_items(max_age_hours: float | None = None):
    items = []
    for item in load_scan(max_age_hours=max_age_hours):
        text = f"{item.name} {item.category} {item.path}".casefold()
        if item.category == "Browsers" or any(name in text for name in BROWSER_NAMES):
            items.append(item)
    return items


def category_bytes(category: str, max_age_hours: float | None = None) -> int:
    return sum(item.bytes_total for item in load_scan(max_age_hours=max_age_hours) if item.category == category)


def build_recommendations() -> list[Recommendation]:
    recs: list[Recommendation] = []
    items = load_scan(max_age_hours=None)
    browser_bytes = sum(item.bytes_total for item in browser_cache_items(max_age_hours=None))
    gaming_bytes = category_bytes("Game", max_age_hours=None)
    windows_bytes = category_bytes("Windows", max_age_hours=None) + category_bytes("System", max_age_hours=None)
    total_cache = sum(item.bytes_total for item in items)
    startup_count = len(list_startup_entries())
    free_pct = disk_percent_free()

    if browser_bytes >= 2 * 1024**3:
        recs.append(Recommendation("high", "Browser cache is large", f"{human_size(browser_bytes)} indexed", "cna clean browser --dry-run"))
    if gaming_bytes >= 2 * 1024**3:
        recs.append(Recommendation("medium", "Gaming/shader cache is large", f"{human_size(gaming_bytes)} indexed", "cna clean gaming --dry-run"))
    if windows_bytes >= 1024**3:
        recs.append(Recommendation("medium", "Windows cache leftovers detected", f"{human_size(windows_bytes)} indexed", "cna clean windows --dry-run"))
    if total_cache >= 5 * 1024**3:
        recs.append(Recommendation("high", "Cleanup index is large", f"{human_size(total_cache)} can be reviewed", "cna clean quick --dry-run"))
    if startup_count >= 12:
        recs.append(Recommendation("high", "Startup impact looks high", f"{startup_count} startup entries", "cna startup"))
    elif startup_count >= 7:
        recs.append(Recommendation("medium", "Startup impact looks moderate", f"{startup_count} startup entries", "cna startup"))
    if free_pct < 0.10:
        recs.append(Recommendation("high", "Disk free space is low", f"{free_pct:.0%} free", "cna storage top C:\\"))
    elif free_pct < 0.15:
        recs.append(Recommendation("medium", "Disk free space is getting tight", f"{free_pct:.0%} free", "cna storage top C:\\"))
    if not items:
        recs.append(Recommendation("medium", "No cleanup index exists yet", "Run a refresh to understand cache size", "cna scan --refresh"))
    return recs


def health_score() -> HealthScore:
    recs = build_recommendations()
    startup_count = len(list_startup_entries())
    total_cache = sum(item.bytes_total for item in load_scan(max_age_hours=None))
    app_count = len(load_app_inventory(max_age_hours=None))
    free_pct = disk_percent_free()

    storage = max(0, min(100, int(free_pct * 500)))
    startup = max(35, 100 - max(0, startup_count - 3) * 5)
    cache = max(40, 100 - int(total_cache / (1024**3)) * 6)
    maintenance = 90 if load_scan(max_age_hours=72) else 60
    apps = 85 if app_count else 65
    total = int(storage * 0.30 + startup * 0.20 + cache * 0.20 + maintenance * 0.20 + apps * 0.10)
    return HealthScore(total, storage, startup, cache, maintenance, apps, recs)


def startup_impact(entry: StartupEntry) -> str:
    text = f"{entry.name} {entry.command}".casefold()
    high_terms = ("steam", "adobe", "teams", "onedrive", "dropbox", "epic", "discord", "launcher", "updater")
    medium_terms = ("spotify", "slack", "zoom", "notion", "drive", "helper")
    if any(term in text for term in high_terms):
        return "High"
    if any(term in text for term in medium_terms):
        return "Medium"
    return "Low"


def disable_startup_entry(name: str, yes: bool = False) -> tuple[bool, str]:
    matches = [entry for entry in list_startup_entries() if name.casefold() in entry.name.casefold()]
    if not matches:
        return False, "No matching startup entry found."
    if len(matches) > 1:
        return False, "Multiple startup entries matched. Use a more exact name."
    entry = matches[0]
    if entry.hive_name == "HKLM" and not is_admin():
        return False, "This startup entry is system-wide. Run an elevated terminal to disable it."
    if not yes:
        return False, f"Dry run: would disable {entry.name} from {entry.location}."
    ok = entry.delete()
    if ok:
        add_history("startup", "ok", f"disabled startup entry {entry.name}")
        return True, f"Disabled startup entry: {entry.name}"
    return False, f"Could not disable startup entry: {entry.name}"


def top_folders(root: Path, limit: int = 15, depth: int = 2) -> list[FolderSize]:
    if not root.exists() or not root.is_dir():
        return []
    rows: list[FolderSize] = []
    try:
        children = [child for child in root.iterdir() if child.is_dir()]
    except OSError:
        return []
    for child in children:
        files, dirs, size, errors = path_size_limited(child, depth=max(0, depth - 1))
        rows.append(FolderSize(child, size, files, dirs, errors))
    return sorted(rows, key=lambda row: row.bytes_total, reverse=True)[:limit]


def path_size_limited(path: Path, depth: int = 2) -> tuple[int, int, int, int]:
    files = dirs = size = errors = 0
    try:
        for entry in path.iterdir():
            try:
                if entry.is_dir() and depth > 0:
                    dirs += 1
                    f, d, s, e = path_size_limited(entry, depth - 1)
                    files += f
                    dirs += d
                    size += s
                    errors += e
                elif entry.is_file():
                    files += 1
                    size += entry.stat().st_size
            except OSError:
                errors += 1
    except OSError:
        errors += 1
    return files, dirs, size, errors


def find_duplicates(root: Path, min_size: int = 1024 * 1024, limit: int = 50) -> list[DuplicateGroup]:
    by_size: dict[int, list[Path]] = {}
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [name for name in dirnames if name not in {".git", ".venv", "node_modules", "Windows", "Program Files"}]
        for filename in filenames:
            path = Path(dirpath) / filename
            try:
                size = path.stat().st_size
            except OSError:
                continue
            if size >= min_size:
                by_size.setdefault(size, []).append(path)
    groups: list[DuplicateGroup] = []
    for size, paths in by_size.items():
        if len(paths) < 2:
            continue
        by_hash: dict[str, list[Path]] = {}
        for path in paths:
            digest = file_digest(path)
            if digest:
                by_hash.setdefault(digest, []).append(path)
        for digest, same in by_hash.items():
            if len(same) > 1:
                groups.append(DuplicateGroup(size, digest, same))
    return sorted(groups, key=lambda group: group.size * (len(group.files) - 1), reverse=True)[:limit]


def file_digest(path: Path) -> str:
    try:
        h = hashlib.sha256()
        with path.open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                h.update(chunk)
        return h.hexdigest()
    except OSError:
        return ""


def audit_downloads(path: Path | None = None, old_days: int = 90) -> DownloadsAudit:
    root = path or (Path.home() / "Downloads")
    cutoff = time.time() - old_days * 86400
    total = old_archives = old_installers = old_images = 0
    names: dict[str, int] = {}
    largest: list[tuple[int, Path]] = []
    if root.exists():
        for child in root.rglob("*"):
            if not child.is_file():
                continue
            try:
                stat = child.stat()
            except OSError:
                continue
            total += stat.st_size
            names[child.name.casefold()] = names.get(child.name.casefold(), 0) + 1
            largest.append((stat.st_size, child))
            if stat.st_mtime < cutoff:
                suffix = child.suffix.casefold()
                if suffix in ARCHIVE_SUFFIXES:
                    old_archives += 1
                elif suffix in INSTALLER_SUFFIXES:
                    old_installers += 1
                elif suffix in IMAGE_SUFFIXES:
                    old_images += 1
    largest_paths = [path for _, path in sorted(largest, reverse=True)[:10]]
    duplicate_names = sum(1 for count in names.values() if count > 1)
    return DownloadsAudit(root, total, old_archives, old_installers, old_images, duplicate_names, largest_paths)


def create_snapshot(name: str = "default") -> int:
    payload = {
        "created_at": time.time(),
        "apps": [app.__dict__ for app in load_app_inventory(max_age_hours=None)],
        "startup": [entry.as_dict() for entry in list_startup_entries()],
        "scan": [item.__dict__ | {"path": str(item.path)} for item in load_scan(max_age_hours=None)],
    }
    return save_snapshot(name, json.dumps(payload, sort_keys=True))


def snapshot_diff(old_payload: str, new_payload: str) -> dict[str, list[str]]:
    old = json.loads(old_payload)
    new = json.loads(new_payload)
    old_apps = {app["name"] for app in old.get("apps", [])}
    new_apps = {app["name"] for app in new.get("apps", [])}
    old_startup = {entry["name"] for entry in old.get("startup", [])}
    new_startup = {entry["name"] for entry in new.get("startup", [])}
    return {
        "apps_added": sorted(new_apps - old_apps),
        "apps_removed": sorted(old_apps - new_apps),
        "startup_added": sorted(new_startup - old_startup),
        "startup_removed": sorted(old_startup - new_startup),
    }


def create_restore_point(description: str = "clean-n-adapt restore point") -> tuple[int, str]:
    command = [
        "powershell",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-Command",
        f"Checkpoint-Computer -Description {json.dumps(description)} -RestorePointType MODIFY_SETTINGS",
    ]
    proc = subprocess.run(command, capture_output=True, text=True, check=False)
    output = (proc.stdout + proc.stderr).strip()
    add_history("restore-point", "ok" if proc.returncode == 0 else "failed", description)
    return proc.returncode, output


def create_schedule(frequency: str, command: str = "cna clean quick --yes") -> tuple[int, str]:
    sc = "WEEKLY" if frequency == "weekly" else "MONTHLY"
    task_name = "clean-n-adapt maintenance"
    proc = subprocess.run(
        ["schtasks", "/Create", "/F", "/TN", task_name, "/SC", sc, "/TR", command],
        capture_output=True,
        text=True,
        check=False,
    )
    add_history("schedule", "ok" if proc.returncode == 0 else "failed", f"{frequency}: {command}")
    return proc.returncode, (proc.stdout + proc.stderr).strip()


def record_storage_history(path: Path | None = None) -> None:
    usage = shutil.disk_usage(path or Path.home().anchor or "C:/")
    upsert_storage_sample(str(path or Path.home().anchor or "C:/"), usage.total, usage.used, usage.free)


def read_storage_history(limit: int = 24):
    return storage_history_rows(limit)
