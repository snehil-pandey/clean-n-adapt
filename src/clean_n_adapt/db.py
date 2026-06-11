from __future__ import annotations

import sqlite3
import time
from pathlib import Path

from .models import ScanItem
from .system import app_state_dir


SCHEMA = """
CREATE TABLE IF NOT EXISTS scans (
    target_key TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    category TEXT NOT NULL,
    path TEXT NOT NULL,
    pattern TEXT NOT NULL,
    files INTEGER NOT NULL,
    dirs INTEGER NOT NULL,
    bytes_total INTEGER NOT NULL,
    scanned_at REAL NOT NULL,
    requires_admin INTEGER NOT NULL,
    errors INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS meta (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
"""


def db_path() -> Path:
    return app_state_dir() / "clean-n-adapt.db"


def connect() -> sqlite3.Connection:
    conn = sqlite3.connect(db_path())
    conn.executescript(SCHEMA)
    return conn


def target_key(path: Path, pattern: str) -> str:
    return f"{str(path).casefold()}::{pattern.casefold()}"


def save_scan(items: list[ScanItem]) -> None:
    with connect() as conn:
        conn.executemany(
            """
            INSERT OR REPLACE INTO scans
            (target_key, name, category, path, pattern, files, dirs, bytes_total, scanned_at, requires_admin, errors)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            [
                (
                    target_key(item.path, item.pattern),
                    item.name,
                    item.category,
                    str(item.path),
                    item.pattern,
                    item.files,
                    item.dirs,
                    item.bytes_total,
                    item.scanned_at,
                    int(item.requires_admin),
                    item.errors,
                )
                for item in items
            ],
        )
        conn.execute(
            "INSERT OR REPLACE INTO meta (key, value) VALUES ('last_scan_at', ?)",
            (str(time.time()),),
        )


def load_scan(max_age_hours: float | None = 24) -> list[ScanItem]:
    with connect() as conn:
        rows = conn.execute(
            """
            SELECT name, category, path, pattern, files, dirs, bytes_total, scanned_at, requires_admin, errors
            FROM scans
            ORDER BY bytes_total DESC
            """
        ).fetchall()
    now = time.time()
    items: list[ScanItem] = []
    for row in rows:
        scanned_at = float(row[7])
        if max_age_hours is not None and now - scanned_at > max_age_hours * 3600:
            continue
        items.append(
            ScanItem(
                name=row[0],
                category=row[1],
                path=Path(row[2]),
                pattern=row[3],
                files=int(row[4]),
                dirs=int(row[5]),
                bytes_total=int(row[6]),
                scanned_at=scanned_at,
                requires_admin=bool(row[8]),
                errors=int(row[9]),
            )
        )
    return items


def clear_db() -> None:
    with connect() as conn:
        conn.execute("DELETE FROM scans")
        conn.execute("DELETE FROM meta")


def scan_stats(max_age_hours: float | None = None) -> tuple[int, int, float | None]:
    items = load_scan(max_age_hours=max_age_hours)
    newest = max((item.scanned_at for item in items), default=None)
    return len(items), sum(item.bytes_total for item in items), newest
