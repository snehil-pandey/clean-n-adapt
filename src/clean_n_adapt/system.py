from __future__ import annotations

import ctypes
import os
import time
from pathlib import Path
from typing import Iterable


def is_admin() -> bool:
    try:
        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:
        return False


def env_path(name: str) -> Path | None:
    value = os.environ.get(name)
    return Path(value).expanduser() if value else None


def app_state_dir() -> Path:
    override = env_path("CNA_STATE_DIR")
    if override:
        override.mkdir(parents=True, exist_ok=True)
        return override
    base = env_path("LOCALAPPDATA") or Path.home() / "AppData" / "Local"
    path = base / "clean-n-adapt"
    path.mkdir(parents=True, exist_ok=True)
    return path


def safe_exists(path: Path) -> bool:
    try:
        return path.exists()
    except OSError:
        return False


def safe_is_dir(path: Path) -> bool:
    try:
        return path.is_dir()
    except OSError:
        return False


def safe_iterdir(path: Path) -> Iterable[Path]:
    try:
        yield from path.iterdir()
    except OSError:
        return


def is_old_enough(path: Path, min_age_seconds: int) -> bool:
    if min_age_seconds <= 0:
        return True
    try:
        newest = max(path.stat().st_mtime, path.stat().st_ctime)
    except OSError:
        return False
    return time.time() - newest >= min_age_seconds
