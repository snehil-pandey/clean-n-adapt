from __future__ import annotations

import fnmatch
import os
import time
from pathlib import Path
from typing import Iterable

from .cleaner import path_size, remove_path
from .db import connect
from .models import CustomRule, ScanItem
from .system import env_path, is_admin, is_old_enough, safe_exists


CATEGORIES = {"Temp", "Browser", "Dev", "Game", "App Cache", "Downloads", "Custom", "Windows"}
RISKS = {"safe", "caution", "dangerous"}
RULE_TYPES = {"folder", "file", "glob"}
CACHE_WORDS = {
    "cache",
    "temp",
    "tmp",
    "thumbnail",
    "thumb",
    "shader",
    "logs",
    "log",
    "build",
    "dist",
    ".pytest_cache",
    "__pycache__",
    "node_modules",
}


def _norm(path: Path) -> Path:
    try:
        return path.expanduser().resolve()
    except OSError:
        return path.expanduser().absolute()


def dangerous_roots() -> set[Path]:
    user = env_path("USERPROFILE") or Path.home()
    windir = env_path("WINDIR") or Path("C:/Windows")
    program_files = env_path("ProgramFiles") or Path("C:/Program Files")
    program_files_x86 = env_path("ProgramFiles(x86)") or Path("C:/Program Files (x86)")
    program_data = env_path("ProgramData") or Path("C:/ProgramData")
    roots = {Path("C:/"), _norm(user), _norm(windir), _norm(program_files), _norm(program_files_x86), _norm(program_data)}
    for drive in "CDEFGHIJKLMNOPQRSTUVWXYZ":
        roots.add(Path(f"{drive}:/"))
    return {_norm(path) for path in roots}


def important_path_warnings(path: Path) -> list[str]:
    normalized = _norm(path)
    parts = {part.casefold() for part in normalized.parts}
    warnings: list[str] = []
    if any(name in parts for name in {"documents", "desktop", "downloads", "pictures", "videos", "music"}):
        warnings.append("Path is inside a personal/user-content folder.")
    if ".git" in parts:
        warnings.append("Path is inside a git repository metadata folder.")
    if any(name in parts for name in {"windows", "program files", "program files (x86)", "programdata"}):
        warnings.append("Path is inside a system/application folder.")
    return warnings


def looks_cache_like(path: Path, pattern: str) -> bool:
    text = f"{path} {pattern}".casefold()
    return any(word in text for word in CACHE_WORDS)


def validate_rule(rule: CustomRule) -> tuple[bool, list[str], list[str]]:
    errors: list[str] = []
    warnings: list[str] = []
    path = _norm(rule.path)
    if rule.rule_type not in RULE_TYPES:
        errors.append("Rule type must be folder, file, or glob.")
    if rule.category not in CATEGORIES:
        errors.append(f"Category must be one of: {', '.join(sorted(CATEGORIES))}.")
    if rule.risk not in RISKS:
        errors.append("Risk must be safe, caution, or dangerous.")
    if path in dangerous_roots():
        errors.append("That path is a blocked root and cannot be used as a custom cleanup rule.")
    if not rule.advanced and not looks_cache_like(path, rule.pattern):
        errors.append("Path does not look cache/temp-like. Re-run with --advanced if you really want this rule.")
    if rule.min_size and rule.max_size and rule.min_size > rule.max_size:
        errors.append("Minimum size cannot be larger than maximum size.")
    warnings.extend(important_path_warnings(path))
    if warnings and not rule.advanced:
        errors.append("Important-looking path requires --advanced.")
    return not errors, errors, warnings


def row_to_rule(row) -> CustomRule:
    return CustomRule(
        id=int(row[0]),
        name=str(row[1]),
        path=Path(row[2]),
        rule_type=str(row[3]),
        pattern=str(row[4]),
        category=str(row[5]),
        recursive=bool(row[6]),
        min_age_hours=float(row[7]),
        min_size=int(row[8]),
        max_size=int(row[9]),
        include_patterns=str(row[10]),
        exclude_patterns=str(row[11]),
        risk=str(row[12]),
        require_admin=bool(row[13]),
        enabled=bool(row[14]),
        notes=str(row[15]),
        advanced=bool(row[16]),
        created_at=float(row[17]),
        updated_at=float(row[18]),
    )


def save_rule(rule: CustomRule) -> int:
    now = time.time()
    with connect() as conn:
        if rule.id:
            conn.execute(
                """
                UPDATE custom_rules
                SET name=?, path=?, rule_type=?, pattern=?, category=?, recursive=?, min_age_hours=?,
                    min_size=?, max_size=?, include_patterns=?, exclude_patterns=?, risk=?, require_admin=?,
                    enabled=?, notes=?, advanced=?, updated_at=?
                WHERE id=?
                """,
                (
                    rule.name,
                    str(rule.path),
                    rule.rule_type,
                    rule.pattern,
                    rule.category,
                    int(rule.recursive),
                    rule.min_age_hours,
                    rule.min_size,
                    rule.max_size,
                    rule.include_patterns,
                    rule.exclude_patterns,
                    rule.risk,
                    int(rule.require_admin),
                    int(rule.enabled),
                    rule.notes,
                    int(rule.advanced),
                    now,
                    rule.id,
                ),
            )
            return int(rule.id)
        cur = conn.execute(
            """
            INSERT INTO custom_rules
            (name, path, rule_type, pattern, category, recursive, min_age_hours, min_size, max_size,
             include_patterns, exclude_patterns, risk, require_admin, enabled, notes, advanced, created_at, updated_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                rule.name,
                str(rule.path),
                rule.rule_type,
                rule.pattern,
                rule.category,
                int(rule.recursive),
                rule.min_age_hours,
                rule.min_size,
                rule.max_size,
                rule.include_patterns,
                rule.exclude_patterns,
                rule.risk,
                int(rule.require_admin),
                int(rule.enabled),
                rule.notes,
                int(rule.advanced),
                now,
                now,
            ),
        )
        return int(cur.lastrowid)


def list_rules(include_disabled: bool = True) -> list[CustomRule]:
    query = """
        SELECT id, name, path, rule_type, pattern, category, recursive, min_age_hours, min_size, max_size,
               include_patterns, exclude_patterns, risk, require_admin, enabled, notes, advanced, created_at, updated_at
        FROM custom_rules
    """
    if not include_disabled:
        query += " WHERE enabled = 1"
    query += " ORDER BY id"
    with connect() as conn:
        rows = conn.execute(query).fetchall()
    return [row_to_rule(row) for row in rows]


def get_rule(rule_id: int) -> CustomRule | None:
    with connect() as conn:
        row = conn.execute(
            """
            SELECT id, name, path, rule_type, pattern, category, recursive, min_age_hours, min_size, max_size,
                   include_patterns, exclude_patterns, risk, require_admin, enabled, notes, advanced, created_at, updated_at
            FROM custom_rules
            WHERE id = ?
            """,
            (rule_id,),
        ).fetchone()
    return None if row is None else row_to_rule(row)


def set_rule_enabled(rule_id: int, enabled: bool) -> bool:
    with connect() as conn:
        cur = conn.execute(
            "UPDATE custom_rules SET enabled = ?, updated_at = ? WHERE id = ?",
            (int(enabled), time.time(), rule_id),
        )
    return cur.rowcount > 0


def delete_rule(rule_id: int) -> bool:
    with connect() as conn:
        cur = conn.execute("DELETE FROM custom_rules WHERE id = ?", (rule_id,))
    return cur.rowcount > 0


def _split_patterns(value: str) -> list[str]:
    return [item.strip() for item in value.split(",") if item.strip()]


def _pattern_ok(path: Path, rule: CustomRule) -> bool:
    name = path.name
    include = _split_patterns(rule.include_patterns)
    exclude = _split_patterns(rule.exclude_patterns)
    if include and not any(fnmatch.fnmatch(name, pattern) for pattern in include):
        return False
    if exclude and any(fnmatch.fnmatch(name, pattern) for pattern in exclude):
        return False
    return True


def _size_ok(path: Path, rule: CustomRule) -> bool:
    try:
        size = path.stat().st_size if path.is_file() else path_size(path)[2]
    except OSError:
        return False
    if rule.min_size and size < rule.min_size:
        return False
    if rule.max_size and size > rule.max_size:
        return False
    return True


def iter_rule_matches(rule: CustomRule) -> Iterable[Path]:
    if rule.require_admin and not is_admin():
        return
    base = rule.path.expanduser()
    if rule.rule_type == "file":
        candidates = [base]
    elif rule.rule_type == "glob":
        if rule.recursive:
            candidates = base.rglob(rule.pattern)
        else:
            candidates = base.glob(rule.pattern)
    else:
        if not safe_exists(base):
            return
        try:
            candidates = base.rglob(rule.pattern) if rule.recursive else base.glob(rule.pattern)
        except OSError:
            return

    min_age_seconds = int(rule.min_age_hours * 3600)
    for candidate in candidates:
        if candidate.is_symlink():
            continue
        if not safe_exists(candidate):
            continue
        if rule.rule_type == "folder" and _norm(candidate) == _norm(base):
            continue
        if is_old_enough(candidate, min_age_seconds) and _pattern_ok(candidate, rule) and _size_ok(candidate, rule):
            yield candidate


def preview_rule(rule: CustomRule) -> ScanItem:
    files = dirs = bytes_total = errors = 0
    for match in iter_rule_matches(rule):
        f_count, d_count, size, err_count = path_size(match)
        files += f_count
        dirs += d_count
        bytes_total += size
        errors += err_count
    return ScanItem(
        name=rule.name,
        category=rule.category,
        path=rule.path,
        pattern=rule.pattern,
        files=files,
        dirs=dirs,
        bytes_total=bytes_total,
        scanned_at=time.time(),
        requires_admin=rule.require_admin,
        errors=errors,
    )


def preview_rules(rules: list[CustomRule]) -> list[ScanItem]:
    return [preview_rule(rule) for rule in rules if rule.enabled]


def clean_rule(rule: CustomRule) -> tuple[int, int, int, list[str]]:
    removed = 0
    failed = 0
    bytes_total = 0
    errors: list[str] = []
    for match in list(iter_rule_matches(rule)):
        _, _, size, _ = path_size(match)
        ok, error = remove_path(match)
        if ok:
            removed += 1
            bytes_total += size
        else:
            failed += 1
            if error and len(errors) < 12:
                errors.append(f"{match}: {error}")
    return removed, failed, bytes_total, errors
