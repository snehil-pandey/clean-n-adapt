from __future__ import annotations

import ctypes
import subprocess
import time

from .boost import disk_cleanup, flush_dns, reset_store, set_power_plan_high_performance
from .cleaner import clean_items, human_size, scan_targets
from .db import add_history, load_scan, save_scan
from .models import ScanItem
from .system import is_admin
from .targets import build_targets


def refresh_explorer() -> tuple[bool, str]:
    subprocess.run(["taskkill", "/f", "/im", "explorer.exe"], capture_output=True, text=True, check=False)
    time.sleep(1)
    subprocess.Popen(["explorer.exe"], shell=False)
    add_history("refresh", "ok", "restarted explorer.exe")
    return True, "Explorer restarted."


def refresh_graphics_driver() -> tuple[bool, str]:
    user32 = ctypes.windll.user32
    key_event = user32.keybd_event
    vk_lwin = 0x5B
    vk_ctrl = 0x11
    vk_shift = 0x10
    vk_b = 0x42
    keyup = 0x0002
    for key in [vk_lwin, vk_ctrl, vk_shift, vk_b]:
        key_event(key, 0, 0, 0)
    for key in [vk_b, vk_shift, vk_ctrl, vk_lwin]:
        key_event(key, 0, keyup, 0)
    add_history("refresh", "ok", "sent Win+Ctrl+Shift+B")
    return True, "Graphics driver refresh hotkey sent."


def refresh_windows_shell(include_graphics: bool = True) -> list[str]:
    messages = [refresh_explorer()[1]]
    if include_graphics:
        messages.append(refresh_graphics_driver()[1])
    return messages


def refresh_cache_index(include_admin: bool = False, min_age_hours: float = 12) -> list[ScanItem]:
    targets = build_targets(include_admin=include_admin and is_admin())
    items = scan_targets(targets, int(max(0, min_age_hours) * 3600))
    save_scan(items)
    add_history("scan", "ok", f"indexed {len(items)} locations", sum(item.bytes_total for item in items), sum(item.files for item in items), sum(item.errors for item in items))
    return items


MODE_CATEGORIES = {
    "quick": {"Temp", "Thumbnails", "Windows", "Browsers", "App Cache"},
    "safe": {"Temp", "Thumbnails", "Windows", "Browsers", "App Cache"},
    "browser": {"Browsers"},
    "dev": {"Dev"},
    "gaming": {"Game"},
    "windows": {"Windows", "System", "Thumbnails"},
    "full": {"Temp", "Thumbnails", "Windows", "Browsers", "App Cache", "Dev", "Game", "System"},
}


def clean_mode(mode: str = "quick", dry_run: bool = True, yes: bool = False) -> tuple[str, int]:
    items = load_scan(max_age_hours=24)
    if not items:
        items = refresh_cache_index(include_admin=False)
    normalized_mode = mode.lower().strip()
    allowed = MODE_CATEGORIES.get(normalized_mode, MODE_CATEGORIES["quick"])
    selected = [item for item in items if item.category in allowed]
    total = sum(item.bytes_total for item in selected)
    label = normalized_mode.title()
    if dry_run or not yes:
        add_history("clean", "dry-run", f"{normalized_mode} clean preview: {human_size(total)}", total, sum(item.files for item in selected), 0)
        return f"{label} clean preview: {human_size(total)} across {len(selected)} locations.", 0
    removed, failed, errors = clean_items(selected, min_age_seconds=12 * 3600)
    add_history("clean", "ok" if failed == 0 else "partial", f"{normalized_mode} clean removed {removed}, failed {failed}", total, removed, failed)
    detail = f"{label} clean complete. Removed {removed} entries. Failed/skipped {failed}."
    if errors:
        detail += "\n" + "\n".join(errors[:8])
    return detail, 0 if failed == 0 else 1


def quick_clean(dry_run: bool = True, yes: bool = False) -> tuple[str, int]:
    return clean_mode("quick", dry_run=dry_run, yes=yes)


def run_boost(kind: str) -> tuple[str, int]:
    actions = {
        "dns": ("Flush DNS", flush_dns),
        "store": ("Reset Store", reset_store),
        "disk": ("Disk Cleanup", disk_cleanup),
        "power": ("High Performance Power Plan", set_power_plan_high_performance),
    }
    if kind == "all":
        messages: list[str] = []
        exit_code = 0
        for key in ["dns", "store", "disk"]:
            message, code = run_boost(key)
            messages.append(message)
            exit_code = max(exit_code, code)
        return "\n".join(messages), exit_code
    label, func = actions.get(kind, actions["dns"])
    code, output = func()
    add_history("boost", "ok" if code == 0 else "failed", label)
    message = f"{label}: {'done' if code == 0 else f'exit code {code}'}"
    if output:
        message += f"\n{output}"
    return message, code
