from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Target:
    name: str
    category: str
    path: Path
    pattern: str = "*"
    requires_admin: bool = False
    description: str = ""


@dataclass
class ScanItem:
    name: str
    category: str
    path: Path
    pattern: str
    files: int
    dirs: int
    bytes_total: int
    scanned_at: float
    requires_admin: bool = False
    errors: int = 0


@dataclass
class AppEntry:
    name: str
    publisher: str
    version: str
    install_location: str
    uninstall_string: str
    quiet_uninstall_string: str
    registry_key: str
