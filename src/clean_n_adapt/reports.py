from __future__ import annotations

import json
from pathlib import Path

from .cleaner import human_size
from .db import all_settings, cleanup_totals, history_rows, load_scan
from .monitor import snapshot, snapshot_dict


def export_json(path: Path) -> Path:
    snap = snapshot(max_age_hours=None)
    data = {
        "snapshot": snapshot_dict(snap),
        "indexed_scan": [
            {
                "name": item.name,
                "category": item.category,
                "path": str(item.path),
                "files": item.files,
                "dirs": item.dirs,
                "bytes_total": item.bytes_total,
                "errors": item.errors,
            }
            for item in load_scan(max_age_hours=None)
        ],
        "history": [
            {
                "id": row[0],
                "action": row[1],
                "status": row[2],
                "summary": row[3],
                "bytes_total": row[4],
                "files_total": row[5],
                "failures": row[6],
                "created_at": row[7],
            }
            for row in history_rows(100)
        ],
        "settings": all_settings(),
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2), encoding="utf-8")
    return path


def export_txt(path: Path) -> Path:
    snap = snapshot(max_age_hours=None)
    bytes_total, files_total, failures = cleanup_totals()
    lines = [
        "clean-n-adapt report",
        "",
        f"Admin: {'yes' if snap.admin else 'no'}",
        f"Indexed cleanup: {human_size(snap.indexed_bytes)} across {snap.indexed_locations} locations",
        f"System drive free: {human_size(snap.system_drive_free)}",
        f"Memory free: {human_size(snap.memory_free)}",
        f"History totals: {human_size(bytes_total)}, files/actions {files_total}, failures {failures}",
        "",
        "Recent history:",
    ]
    for row in history_rows(25):
        lines.append(f"- #{row[0]} {row[1]} {row[2]}: {row[3]}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path
