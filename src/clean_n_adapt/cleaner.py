from __future__ import annotations

import os
import shutil
import time
from pathlib import Path
from typing import Iterable

from rich.console import Console
from rich.progress import BarColumn, Progress, SpinnerColumn, TaskProgressColumn, TextColumn

from .models import ScanItem, Target
from .system import is_old_enough, safe_exists


console = Console()


def iter_matches(target: Target | ScanItem, min_age_seconds: int) -> Iterable[Path]:
    if not safe_exists(target.path):
        return
    try:
        iterator = target.path.iterdir() if target.pattern == "*" else target.path.glob(target.pattern)
        for item in iterator:
            if item.is_symlink():
                continue
            if is_old_enough(item, min_age_seconds):
                yield item
    except OSError:
        return


def path_size(path: Path) -> tuple[int, int, int, int]:
    files = 0
    dirs = 0
    size = 0
    errors = 0
    try:
        if path.is_file():
            return 1, 0, path.stat().st_size, 0
        if path.is_dir():
            dirs += 1
            for root, dirnames, filenames in os.walk(path, followlinks=False):
                dirs += len(dirnames)
                for filename in filenames:
                    file_path = Path(root) / filename
                    try:
                        files += 1
                        size += file_path.stat().st_size
                    except OSError:
                        errors += 1
    except OSError:
        errors += 1
    return files, dirs, size, errors


def scan_targets(targets: list[Target], min_age_seconds: int) -> list[ScanItem]:
    results: list[ScanItem] = []
    scanned_at = time.time()
    with Progress(
        SpinnerColumn(),
        TextColumn("[progress.description]{task.description}"),
        BarColumn(),
        TaskProgressColumn(),
        console=console,
    ) as progress:
        task = progress.add_task("Scanning known cache/temp locations", total=len(targets))
        for target in targets:
            files = dirs = bytes_total = errors = 0
            for match in iter_matches(target, min_age_seconds):
                f_count, d_count, size, err_count = path_size(match)
                files += f_count
                dirs += d_count
                bytes_total += size
                errors += err_count
            if files or dirs or errors:
                results.append(
                    ScanItem(
                        name=target.name,
                        category=target.category,
                        path=target.path,
                        pattern=target.pattern,
                        files=files,
                        dirs=dirs,
                        bytes_total=bytes_total,
                        scanned_at=scanned_at,
                        requires_admin=target.requires_admin,
                        errors=errors,
                    )
                )
            progress.advance(task)
    return results


def remove_path(path: Path) -> tuple[bool, str | None]:
    try:
        if path.is_file() or path.is_symlink():
            path.unlink()
        elif path.is_dir():
            shutil.rmtree(path, ignore_errors=False)
        return True, None
    except OSError as exc:
        return False, str(exc)


def clean_items(items: list[ScanItem], min_age_seconds: int) -> tuple[int, int, list[str]]:
    removed = 0
    failed = 0
    errors: list[str] = []
    with Progress(
        SpinnerColumn(),
        TextColumn("[progress.description]{task.description}"),
        BarColumn(),
        TaskProgressColumn(),
        console=console,
    ) as progress:
        task = progress.add_task("Cleaning cached disposable paths", total=len(items))
        for item in items:
            for match in iter_matches(item, min_age_seconds):
                ok, error = remove_path(match)
                if ok:
                    removed += 1
                else:
                    failed += 1
                    if error and len(errors) < 12:
                        errors.append(f"{match}: {error}")
            progress.advance(task)
    return removed, failed, errors


def human_size(value: int) -> str:
    units = ["B", "KB", "MB", "GB", "TB"]
    size = float(value)
    for unit in units:
        if size < 1024 or unit == units[-1]:
            return f"{size:.1f} {unit}" if unit != "B" else f"{int(size)} B"
        size /= 1024
    return f"{value} B"
