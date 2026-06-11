from __future__ import annotations

import subprocess


def run_command(args: list[str]) -> tuple[int, str]:
    try:
        proc = subprocess.run(args, check=False, capture_output=True, text=True)
        output = (proc.stdout or proc.stderr or "").strip()
        return proc.returncode, output
    except OSError as exc:
        return 1, str(exc)


def flush_dns() -> tuple[int, str]:
    return run_command(["ipconfig", "/flushdns"])


def reset_store() -> tuple[int, str]:
    return run_command(["WSReset.exe", "-i"])


def disk_cleanup() -> tuple[int, str]:
    return run_command(["cleanmgr", "/verylowdisk"])


def set_power_plan_high_performance() -> tuple[int, str]:
    return run_command(["powercfg", "/setactive", "SCHEME_MIN"])
