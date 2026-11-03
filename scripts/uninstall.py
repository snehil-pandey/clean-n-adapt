from __future__ import annotations

import argparse
import ctypes
import os
import shutil
import subprocess
import sys
import tempfile
import uuid
import winreg
from pathlib import Path


APP_NAME = "Clean-n-Adapt"
REG_NAME = "CleanNAdapt"


def is_admin() -> bool:
    try:
        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:
        return False


def normalize_path(value: str) -> str:
    return os.path.expandvars(value).strip().strip('"').rstrip("\\/").casefold()


def broadcast_environment_change() -> None:
    result = ctypes.c_ulong()
    ctypes.windll.user32.SendMessageTimeoutW(0xFFFF, 0x1A, 0, "Environment", 0x2, 5000, ctypes.byref(result))


def env_root(scope: str):
    if scope == "Machine":
        return winreg.HKEY_LOCAL_MACHINE, r"SYSTEM\CurrentControlSet\Control\Session Manager\Environment"
    return winreg.HKEY_CURRENT_USER, "Environment"


def remove_from_path(install_dir: Path, scope: str) -> None:
    root, subkey = env_root(scope)
    with winreg.OpenKey(root, subkey, 0, winreg.KEY_READ | winreg.KEY_WRITE) as key:
        try:
            current, value_type = winreg.QueryValueEx(key, "Path")
        except FileNotFoundError:
            return
        install_norm = normalize_path(str(install_dir))
        kept = [part.strip() for part in str(current).split(";") if part.strip() and normalize_path(part) != install_norm]
        winreg.SetValueEx(key, "Path", 0, value_type, ";".join(kept))
    broadcast_environment_change()


def remove_tree(path: Path) -> None:
    if path.exists():
        shutil.rmtree(path, ignore_errors=True)


def relaunch_from_temp(install_dir: Path, args: argparse.Namespace) -> bool:
    executable = Path(sys.argv[0]).resolve()
    if executable.suffix.casefold() != ".exe" or install_dir not in executable.parents:
        return False
    temp_dir = Path(tempfile.gettempdir()) / "CleanNAdapt" / uuid.uuid4().hex
    temp_dir.mkdir(parents=True, exist_ok=True)
    temp_exe = temp_dir / executable.name
    shutil.copy2(executable, temp_exe)
    command = [str(temp_exe), "--install-dir", str(install_dir), "--path-scope", args.path_scope, "--relocated"]
    if args.keep_state:
        command.append("--keep-state")
    subprocess.Popen(command, close_fds=True)
    return True


def schedule_self_cleanup() -> None:
    executable = Path(sys.argv[0]).resolve()
    if executable.suffix.casefold() == ".exe" and "CleanNAdapt" in executable.parts:
        ctypes.windll.kernel32.MoveFileExW(str(executable), None, 0x4)


def remove_shortcuts(scope: str) -> None:
    roots = [
        Path(os.environ.get("ProgramData", r"C:\ProgramData")) / "Microsoft" / "Windows" / "Start Menu" / "Programs" / APP_NAME,
        Path(os.environ.get("APPDATA", "")) / "Microsoft" / "Windows" / "Start Menu" / "Programs" / APP_NAME,
        Path(os.environ.get("PUBLIC", r"C:\Users\Public")) / "Desktop" / f"{APP_NAME}.url",
        Path.home() / "Desktop" / f"{APP_NAME}.url",
        Path(os.environ.get("LOCALAPPDATA", "")) / "Microsoft" / "Windows Terminal" / "Fragments" / "CleanNAdapt",
    ]
    for root in roots:
        if root.is_dir():
            remove_tree(root)
        elif root.exists():
            root.unlink(missing_ok=True)


def unregister_uninstall(scope: str) -> None:
    root = winreg.HKEY_LOCAL_MACHINE if scope == "Machine" else winreg.HKEY_CURRENT_USER
    subkey = rf"Software\Microsoft\Windows\CurrentVersion\Uninstall\{REG_NAME}"
    try:
        winreg.DeleteKey(root, subkey)
    except FileNotFoundError:
        pass


def uninstall(args: argparse.Namespace) -> int:
    install_dir = Path(args.install_dir).expanduser().resolve()
    if not args.relocated and relaunch_from_temp(install_dir, args):
        print(f"Finishing {APP_NAME} uninstall in the background.")
        return 0
    if args.path_scope == "Machine" and not is_admin():
        script = Path(sys.argv[0]).resolve()
        executable = str(script) if script.suffix.casefold() == ".exe" else sys.executable
        params = f'"{script}" --install-dir "{install_dir}" --path-scope Machine'
        if script.suffix.casefold() == ".exe":
            params = f'--install-dir "{install_dir}" --path-scope Machine'
        if args.keep_state:
            params += " --keep-state"
        result = ctypes.windll.shell32.ShellExecuteW(None, "runas", executable, params, None, 1)
        return 0 if result > 32 else 1

    remove_from_path(install_dir, args.path_scope)
    remove_shortcuts(args.path_scope)
    unregister_uninstall(args.path_scope)
    if args.keep_state:
        for child in install_dir.iterdir() if install_dir.exists() else []:
            if child.name != ".state":
                if child.is_dir():
                    remove_tree(child)
                else:
                    child.unlink(missing_ok=True)
    else:
        remove_tree(install_dir)
    schedule_self_cleanup()
    print(f"Uninstalled {APP_NAME} from {install_dir}")
    return 0


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=f"Uninstall {APP_NAME}.")
    parser.add_argument("--install-dir", type=Path, default=Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "cleanNadapt")
    parser.add_argument("--path-scope", choices=["User", "Machine"], default="Machine")
    parser.add_argument("--keep-state", action="store_true")
    parser.add_argument("--relocated", action="store_true", help=argparse.SUPPRESS)
    return parser.parse_args(argv)


if __name__ == "__main__":
    raise SystemExit(uninstall(parse_args()))
