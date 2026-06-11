from __future__ import annotations

import sqlite3
import time
from pathlib import Path

from .models import AppEntry, ScanItem
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
CREATE TABLE IF NOT EXISTS settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS custom_rules (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    path TEXT NOT NULL,
    rule_type TEXT NOT NULL,
    pattern TEXT NOT NULL,
    category TEXT NOT NULL,
    recursive INTEGER NOT NULL,
    min_age_hours REAL NOT NULL,
    min_size INTEGER NOT NULL,
    max_size INTEGER NOT NULL,
    include_patterns TEXT NOT NULL,
    exclude_patterns TEXT NOT NULL,
    risk TEXT NOT NULL,
    require_admin INTEGER NOT NULL,
    enabled INTEGER NOT NULL,
    notes TEXT NOT NULL,
    advanced INTEGER NOT NULL DEFAULT 0,
    created_at REAL NOT NULL,
    updated_at REAL NOT NULL
);
CREATE TABLE IF NOT EXISTS history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    action TEXT NOT NULL,
    status TEXT NOT NULL,
    summary TEXT NOT NULL,
    bytes_total INTEGER NOT NULL DEFAULT 0,
    files_total INTEGER NOT NULL DEFAULT 0,
    failures INTEGER NOT NULL DEFAULT 0,
    created_at REAL NOT NULL
);
CREATE TABLE IF NOT EXISTS cleanup_results (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    history_id INTEGER NOT NULL,
    mode TEXT NOT NULL,
    target TEXT NOT NULL,
    path TEXT NOT NULL,
    bytes_total INTEGER NOT NULL DEFAULT 0,
    files_total INTEGER NOT NULL DEFAULT 0,
    failures INTEGER NOT NULL DEFAULT 0,
    created_at REAL NOT NULL,
    FOREIGN KEY(history_id) REFERENCES history(id)
);
CREATE TABLE IF NOT EXISTS app_inventory (
    registry_key TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    publisher TEXT NOT NULL,
    version TEXT NOT NULL,
    install_location TEXT NOT NULL,
    uninstall_string TEXT NOT NULL,
    quiet_uninstall_string TEXT NOT NULL,
    install_scope TEXT NOT NULL,
    app_kind TEXT NOT NULL,
    scanned_at REAL NOT NULL
);
CREATE TABLE IF NOT EXISTS snapshots (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    payload TEXT NOT NULL,
    created_at REAL NOT NULL
);
CREATE TABLE IF NOT EXISTS storage_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    root TEXT NOT NULL,
    total_bytes INTEGER NOT NULL,
    used_bytes INTEGER NOT NULL,
    free_bytes INTEGER NOT NULL,
    created_at REAL NOT NULL
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


def get_setting(key: str, default: str = "") -> str:
    with connect() as conn:
        row = conn.execute("SELECT value FROM settings WHERE key = ?", (key,)).fetchone()
    return default if row is None else str(row[0])


def set_setting(key: str, value: str) -> None:
    with connect() as conn:
        conn.execute(
            "INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)",
            (key, value),
        )


def all_settings() -> dict[str, str]:
    with connect() as conn:
        rows = conn.execute("SELECT key, value FROM settings ORDER BY key").fetchall()
    return {str(key): str(value) for key, value in rows}


def add_history(action: str, status: str, summary: str, bytes_total: int = 0, files_total: int = 0, failures: int = 0) -> int:
    with connect() as conn:
        cur = conn.execute(
            """
            INSERT INTO history (action, status, summary, bytes_total, files_total, failures, created_at)
            VALUES (?, ?, ?, ?, ?, ?, ?)
            """,
            (action, status, summary, bytes_total, files_total, failures, time.time()),
        )
        return int(cur.lastrowid)


def add_cleanup_result(history_id: int, mode: str, target: str, path: str, bytes_total: int, files_total: int, failures: int) -> None:
    with connect() as conn:
        conn.execute(
            """
            INSERT INTO cleanup_results (history_id, mode, target, path, bytes_total, files_total, failures, created_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (history_id, mode, target, path, bytes_total, files_total, failures, time.time()),
        )


def history_rows(limit: int = 25) -> list[tuple]:
    with connect() as conn:
        return conn.execute(
            """
            SELECT id, action, status, summary, bytes_total, files_total, failures, created_at
            FROM history
            ORDER BY id DESC
            LIMIT ?
            """,
            (limit,),
        ).fetchall()


def cleanup_totals() -> tuple[int, int, int]:
    with connect() as conn:
        row = conn.execute(
            "SELECT COALESCE(SUM(bytes_total), 0), COALESCE(SUM(files_total), 0), COALESCE(SUM(failures), 0) FROM history"
        ).fetchone()
    return int(row[0]), int(row[1]), int(row[2])


def save_app_inventory(apps: list[AppEntry]) -> None:
    with connect() as conn:
        conn.execute("DELETE FROM app_inventory")
        conn.executemany(
            """
            INSERT OR REPLACE INTO app_inventory
            (registry_key, name, publisher, version, install_location, uninstall_string, quiet_uninstall_string,
             install_scope, app_kind, scanned_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            [
                (
                    app.registry_key,
                    app.name,
                    app.publisher,
                    app.version,
                    app.install_location,
                    app.uninstall_string,
                    app.quiet_uninstall_string,
                    app.install_scope,
                    app.app_kind,
                    app.scanned_at,
                )
                for app in apps
            ],
        )
        conn.execute("INSERT OR REPLACE INTO meta (key, value) VALUES ('last_app_scan_at', ?)", (str(time.time()),))


def load_app_inventory(max_age_hours: float | None = None) -> list[AppEntry]:
    with connect() as conn:
        rows = conn.execute(
            """
            SELECT name, publisher, version, install_location, uninstall_string, quiet_uninstall_string,
                   registry_key, install_scope, app_kind, scanned_at
            FROM app_inventory
            ORDER BY install_scope, app_kind, name
            """
        ).fetchall()
    now = time.time()
    apps: list[AppEntry] = []
    for row in rows:
        scanned_at = float(row[9])
        if max_age_hours is not None and now - scanned_at > max_age_hours * 3600:
            continue
        apps.append(
            AppEntry(
                name=row[0],
                publisher=row[1],
                version=row[2],
                install_location=row[3],
                uninstall_string=row[4],
                quiet_uninstall_string=row[5],
                registry_key=row[6],
                install_scope=row[7],
                app_kind=row[8],
                scanned_at=scanned_at,
            )
        )
    return apps


def save_snapshot(name: str, payload: str) -> int:
    with connect() as conn:
        cur = conn.execute(
            "INSERT INTO snapshots (name, payload, created_at) VALUES (?, ?, ?)",
            (name, payload, time.time()),
        )
        return int(cur.lastrowid)


def latest_snapshots(limit: int = 2, name: str | None = None) -> list[tuple]:
    sql = "SELECT id, name, payload, created_at FROM snapshots"
    params: tuple = ()
    if name:
        sql += " WHERE name = ?"
        params = (name,)
    sql += " ORDER BY id DESC LIMIT ?"
    params = (*params, limit)
    with connect() as conn:
        return conn.execute(sql, params).fetchall()


def upsert_storage_sample(root: str, total_bytes: int, used_bytes: int, free_bytes: int) -> int:
    with connect() as conn:
        cur = conn.execute(
            """
            INSERT INTO storage_history (root, total_bytes, used_bytes, free_bytes, created_at)
            VALUES (?, ?, ?, ?, ?)
            """,
            (root, total_bytes, used_bytes, free_bytes, time.time()),
        )
        return int(cur.lastrowid)


def storage_history_rows(limit: int = 24) -> list[tuple]:
    with connect() as conn:
        return conn.execute(
            """
            SELECT id, root, total_bytes, used_bytes, free_bytes, created_at
            FROM storage_history
            ORDER BY id DESC
            LIMIT ?
            """,
            (limit,),
        ).fetchall()
