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
    install_scope: str = "system"
    app_kind: str = "desktop"
    scanned_at: float = 0


@dataclass
class CustomRule:
    id: int | None
    name: str
    path: Path
    rule_type: str
    pattern: str
    category: str
    recursive: bool
    min_age_hours: float
    min_size: int
    max_size: int
    include_patterns: str
    exclude_patterns: str
    risk: str
    require_admin: bool
    enabled: bool
    notes: str
    advanced: bool = False
    created_at: float = 0
    updated_at: float = 0


@dataclass
class HistoryEntry:
    id: int
    action: str
    status: str
    summary: str
    bytes_total: int
    files_total: int
    failures: int
    created_at: float
