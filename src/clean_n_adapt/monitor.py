from __future__ import annotations

import ctypes
import shutil
import time
from dataclasses import asdict, dataclass
from pathlib import Path

from rich.console import Console
from rich.table import Table

from .cleaner import human_size
from .db import db_path, load_scan
from .system import env_path, is_admin


console = Console()


@dataclass
class MonitorSnapshot:
    admin: bool
    db_path: str
    indexed_locations: int
    indexed_bytes: int
    index_age_seconds: int | None
    system_drive_total: int
    system_drive_used: int
    system_drive_free: int
    memory_total: int
    memory_used: int
    memory_free: int


class MEMORYSTATUSEX(ctypes.Structure):
    _fields_ = [
        ("dwLength", ctypes.c_ulong),
        ("dwMemoryLoad", ctypes.c_ulong),
        ("ullTotalPhys", ctypes.c_ulonglong),
        ("ullAvailPhys", ctypes.c_ulonglong),
        ("ullTotalPageFile", ctypes.c_ulonglong),
        ("ullAvailPageFile", ctypes.c_ulonglong),
        ("ullTotalVirtual", ctypes.c_ulonglong),
        ("ullAvailVirtual", ctypes.c_ulonglong),
        ("sullAvailExtendedVirtual", ctypes.c_ulonglong),
    ]


def memory_usage() -> tuple[int, int, int]:
    stat = MEMORYSTATUSEX()
    stat.dwLength = ctypes.sizeof(MEMORYSTATUSEX)
    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(stat)):
        return 0, 0, 0
    used = int(stat.ullTotalPhys - stat.ullAvailPhys)
    return int(stat.ullTotalPhys), used, int(stat.ullAvailPhys)


def system_drive() -> Path:
    system_root = env_path("SystemRoot") or env_path("WINDIR") or Path("C:/Windows")
    return Path(system_root.anchor or "C:/")


def snapshot(max_age_hours: float | None = None) -> MonitorSnapshot:
    items = load_scan(max_age_hours=max_age_hours)
    newest = max((item.scanned_at for item in items), default=None)
    drive = shutil.disk_usage(system_drive())
    mem_total, mem_used, mem_free = memory_usage()
    return MonitorSnapshot(
        admin=is_admin(),
        db_path=str(db_path()),
        indexed_locations=len(items),
        indexed_bytes=sum(item.bytes_total for item in items),
        index_age_seconds=None if newest is None else max(0, int(time.time() - newest)),
        system_drive_total=drive.total,
        system_drive_used=drive.used,
        system_drive_free=drive.free,
        memory_total=mem_total,
        memory_used=mem_used,
        memory_free=mem_free,
    )


def snapshot_dict(item: MonitorSnapshot) -> dict:
    data = asdict(item)
    data["indexed_size"] = human_size(item.indexed_bytes)
    data["system_drive_free_size"] = human_size(item.system_drive_free)
    data["memory_free_size"] = human_size(item.memory_free)
    return data


def print_snapshot(item: MonitorSnapshot, compact: bool = False) -> None:
    if compact:
        age = "never" if item.index_age_seconds is None else f"{item.index_age_seconds // 60}m"
        console.print(
            f"admin={'yes' if item.admin else 'no'} | "
            f"index={item.indexed_locations} locations/{human_size(item.indexed_bytes)} age={age} | "
            f"disk_free={human_size(item.system_drive_free)} | "
            f"mem_free={human_size(item.memory_free)}"
        )
        return

    table = Table(title="clean-n-adapt monitor")
    table.add_column("Thing", style="cyan")
    table.add_column("Value", style="green")
    age = "no index yet" if item.index_age_seconds is None else f"{item.index_age_seconds // 60} minutes"
    table.add_row("Admin", "yes" if item.admin else "no")
    table.add_row("Cache index", f"{item.indexed_locations} locations, {human_size(item.indexed_bytes)}, age {age}")
    table.add_row("System drive used", f"{human_size(item.system_drive_used)} / {human_size(item.system_drive_total)}")
    table.add_row("System drive free", human_size(item.system_drive_free))
    table.add_row("Memory used", f"{human_size(item.memory_used)} / {human_size(item.memory_total)}")
    table.add_row("Memory free", human_size(item.memory_free))
    table.add_row("DB", item.db_path)
    console.print(table)
