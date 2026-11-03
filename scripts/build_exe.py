from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VENV = ROOT / ".venv"
PYTHON = VENV / "Scripts" / "python.exe"


def run(command: list[str]) -> None:
    print("+ " + subprocess.list2cmdline(command))
    subprocess.run(command, cwd=ROOT, check=True)


def build_onefile(source: str, output: str, *, pyside: bool = False) -> None:
    command = [
        str(PYTHON),
        "-m",
        "nuitka",
        "--standalone",
        "--onefile",
        "--assume-yes-for-downloads",
        f"--output-filename={output}",
        "--output-dir=dist",
    ]
    if pyside:
        command.extend(["--windows-uac-admin", "--enable-plugin=pyside6", "--python-flag=-m"])
    command.append(source)
    run(command)


def main() -> int:
    if not VENV.exists():
        run([sys.executable, "-m", "venv", str(VENV)])
    run([str(PYTHON), "-m", "pip", "install", "--upgrade", "pip"])
    run([str(PYTHON), "-m", "pip", "install", "-e", ".[build]"])
    build_onefile("src\\clean_n_adapt", "cna.exe", pyside=True)
    build_onefile("scripts\\install.py", "clean-n-adapt-install.exe")
    build_onefile("scripts\\uninstall.py", "clean-n-adapt-uninstall.exe")
    print()
    print("Built dist\\cna.exe, clean-n-adapt-install.exe, and clean-n-adapt-uninstall.exe")
    print("Package with: python scripts\\package_release.py")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
