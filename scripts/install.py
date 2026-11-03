from __future__ import annotations

import argparse
import ctypes
import json
import os
import shutil
import subprocess
import sys
import winreg
from pathlib import Path


APP_NAME = "Clean-n-Adapt"
REG_NAME = "CleanNAdapt"
VERSION = "2.0.0"


def runtime_dir() -> Path:
    return Path(sys.argv[0]).resolve().parent


def is_admin() -> bool:
    try:
        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:
        return False


def default_install_dir() -> Path:
    return Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "cleanNadapt"


def resolve_source_exe(script_dir: Path) -> Path:
    repo_root = script_dir.parent
    candidates = [
        script_dir / "cna.exe",
        repo_root / "dist" / "cna.exe",
        Path.cwd() / "dist" / "cna.exe",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate.resolve()
    expected = "\n  ".join(str(path) for path in candidates)
    raise FileNotFoundError(
        "cna.exe was not found.\n\n"
        f"Expected one of:\n  {expected}\n\n"
        "Release ZIP layout: clean-n-adapt-install.exe beside cna.exe.\n"
        "Repo/dev layout: run python scripts/build_exe.py first."
    )


def resolve_companion(script_dir: Path) -> Path:
    for candidate in (script_dir / "clean-n-adapt-uninstall.exe", script_dir / "uninstall.py"):
        if candidate.exists():
            return candidate.resolve()
    raise FileNotFoundError("Required uninstaller was not found beside the installer.")


def relaunch_as_admin(args: argparse.Namespace) -> int:
    script = Path(sys.argv[0]).resolve()
    command = str(script) if script.suffix.casefold() == ".exe" else sys.executable
    cmd = [command, str(script), "--install-dir", str(args.install_dir), "--path-scope", args.path_scope]
    if script.suffix.casefold() == ".exe":
        cmd.pop(1)
    if args.no_path:
        cmd.append("--no-path")
    if args.desktop_shortcut:
        cmd.append("--desktop-shortcut")
    params = subprocess.list2cmdline(cmd[1:])
    result = ctypes.windll.shell32.ShellExecuteW(None, "runas", cmd[0], params, None, 1)
    return 0 if result > 32 else 1


def normalize_path(value: str) -> str:
    return os.path.expandvars(value).strip().strip('"').rstrip("\\/").casefold()


def broadcast_environment_change() -> None:
    result = ctypes.c_ulong()
    ctypes.windll.user32.SendMessageTimeoutW(0xFFFF, 0x1A, 0, "Environment", 0x2, 5000, ctypes.byref(result))


def env_root(scope: str):
    if scope == "Machine":
        return winreg.HKEY_LOCAL_MACHINE, r"SYSTEM\CurrentControlSet\Control\Session Manager\Environment"
    return winreg.HKEY_CURRENT_USER, "Environment"


def add_to_path(install_dir: Path, scope: str) -> None:
    root, subkey = env_root(scope)
    with winreg.OpenKey(root, subkey, 0, winreg.KEY_READ | winreg.KEY_WRITE) as key:
        try:
            current, value_type = winreg.QueryValueEx(key, "Path")
        except FileNotFoundError:
            current, value_type = "", winreg.REG_EXPAND_SZ
        parts = [part for part in str(current).split(";") if part.strip()]
        install_norm = normalize_path(str(install_dir))
        kept = [part.strip() for part in parts if normalize_path(part) != install_norm]
        new_value = ";".join([str(install_dir)] + kept)
        winreg.SetValueEx(key, "Path", 0, value_type, new_value)
    os.environ["Path"] = f"{install_dir};{os.environ.get('Path', '')}"
    broadcast_environment_change()


def create_url_shortcut(path: Path, target: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(f"[InternetShortcut]\nURL=file:///{target.as_posix()}\nIconFile={target}\nIconIndex=0\n", encoding="utf-8")


def start_menu_dir(machine: bool) -> Path:
    if machine:
        base = Path(os.environ.get("ProgramData", r"C:\ProgramData")) / "Microsoft" / "Windows" / "Start Menu" / "Programs"
    else:
        base = Path(os.environ["APPDATA"]) / "Microsoft" / "Windows" / "Start Menu" / "Programs"
    return base / APP_NAME


def desktop_dir(machine: bool) -> Path:
    if machine:
        return Path(os.environ.get("PUBLIC", r"C:\Users\Public")) / "Desktop"
    return Path.home() / "Desktop"


def write_terminal_profile(install_dir: Path, exe: Path) -> None:
    root = Path(os.environ.get("LOCALAPPDATA", "")) / "Microsoft" / "Windows Terminal" / "Fragments" / "CleanNAdapt"
    root.mkdir(parents=True, exist_ok=True)
    profile = {
        "profiles": [
            {
                "name": APP_NAME,
                "commandline": f'"{exe}"',
                "startingDirectory": str(install_dir),
                "icon": str(exe),
                "hidden": False,
            }
        ]
    }
    (root / "clean-n-adapt.json").write_text(json.dumps(profile, indent=2), encoding="utf-8")


def register_uninstall(install_dir: Path, scope: str) -> None:
    root = winreg.HKEY_LOCAL_MACHINE if scope == "Machine" else winreg.HKEY_CURRENT_USER
    subkey = rf"Software\Microsoft\Windows\CurrentVersion\Uninstall\{REG_NAME}"
    exe = install_dir / "cna.exe"
    native_uninstaller = install_dir / "clean-n-adapt-uninstall.exe"
    if native_uninstaller.exists():
        command = f'"{native_uninstaller}" --install-dir "{install_dir}" --path-scope {scope}'
    else:
        uninstall = install_dir / "uninstall.py"
        command = f'"{sys.executable}" "{uninstall}" --install-dir "{install_dir}" --path-scope {scope}'
    with winreg.CreateKey(root, subkey) as key:
        winreg.SetValueEx(key, "DisplayName", 0, winreg.REG_SZ, APP_NAME)
        winreg.SetValueEx(key, "DisplayVersion", 0, winreg.REG_SZ, VERSION)
        winreg.SetValueEx(key, "Publisher", 0, winreg.REG_SZ, "snehil-pandey")
        winreg.SetValueEx(key, "InstallLocation", 0, winreg.REG_SZ, str(install_dir))
        winreg.SetValueEx(key, "DisplayIcon", 0, winreg.REG_SZ, str(exe))
        winreg.SetValueEx(key, "UninstallString", 0, winreg.REG_SZ, command)


def install(args: argparse.Namespace) -> int:
    script_dir = runtime_dir()
    install_dir = Path(args.install_dir).expanduser().resolve()
    needs_admin = args.path_scope == "Machine" or str(install_dir).casefold().startswith(str(default_install_dir().parent).casefold())
    if needs_admin and not is_admin():
        print(f"{APP_NAME} needs administrator permission to install into:")
        print(f"  {install_dir}")
        return relaunch_as_admin(args)

    source_exe = resolve_source_exe(script_dir)
    install_dir.mkdir(parents=True, exist_ok=True)
    (install_dir / ".state").mkdir(parents=True, exist_ok=True)
    shutil.copy2(source_exe, install_dir / "cna.exe")
    companion = resolve_companion(script_dir)
    destination_name = "clean-n-adapt-uninstall.exe" if companion.suffix.casefold() == ".exe" else "uninstall.py"
    shutil.copy2(companion, install_dir / destination_name)

    if not args.no_path:
        add_to_path(install_dir, args.path_scope)

    machine = args.path_scope == "Machine" and is_admin()
    create_url_shortcut(start_menu_dir(machine) / f"{APP_NAME}.url", install_dir / "cna.exe")
    if args.desktop_shortcut:
        create_url_shortcut(desktop_dir(machine) / f"{APP_NAME}.url", install_dir / "cna.exe")
    write_terminal_profile(install_dir, install_dir / "cna.exe")
    register_uninstall(install_dir, args.path_scope)

    print()
    print(f"Installed {APP_NAME} to {install_dir}")
    print(f"State DB folder: {install_dir / '.state'}")
    print("Open a new terminal and run: cna")
    print(f"Or run directly: {install_dir / 'cna.exe'}")
    return 0


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=f"Install {APP_NAME}.")
    parser.add_argument("--install-dir", type=Path, default=default_install_dir())
    parser.add_argument("--path-scope", choices=["User", "Machine"], default="Machine")
    parser.add_argument("--no-path", action="store_true")
    parser.add_argument("--desktop-shortcut", action="store_true")
    return parser.parse_args(argv)


if __name__ == "__main__":
    raise SystemExit(install(parse_args()))
