from __future__ import annotations

import subprocess
import winreg

from .models import AppEntry


UNINSTALL_ROOTS = [
    (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall"),
    (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall"),
    (winreg.HKEY_CURRENT_USER, r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall"),
]


def read_value(key: winreg.HKEYType, name: str) -> str:
    try:
        value, _ = winreg.QueryValueEx(key, name)
        return str(value).strip()
    except OSError:
        return ""


def installed_apps() -> list[AppEntry]:
    apps: list[AppEntry] = []
    for hive, root in UNINSTALL_ROOTS:
        try:
            with winreg.OpenKey(hive, root) as root_key:
                count = winreg.QueryInfoKey(root_key)[0]
                for index in range(count):
                    try:
                        subkey_name = winreg.EnumKey(root_key, index)
                        key_path = f"{root}\\{subkey_name}"
                        with winreg.OpenKey(hive, key_path) as app_key:
                            name = read_value(app_key, "DisplayName")
                            if not name:
                                continue
                            system_component = read_value(app_key, "SystemComponent")
                            if system_component == "1":
                                continue
                            apps.append(
                                AppEntry(
                                    name=name,
                                    publisher=read_value(app_key, "Publisher"),
                                    version=read_value(app_key, "DisplayVersion"),
                                    install_location=read_value(app_key, "InstallLocation"),
                                    uninstall_string=read_value(app_key, "UninstallString"),
                                    quiet_uninstall_string=read_value(app_key, "QuietUninstallString"),
                                    registry_key=key_path,
                                )
                            )
                    except OSError:
                        continue
        except OSError:
            continue
    return sorted(apps, key=lambda app: app.name.casefold())


def find_apps(query: str) -> list[AppEntry]:
    needle = query.casefold()
    return [app for app in installed_apps() if needle in app.name.casefold()]


def uninstall_app(app: AppEntry, quiet: bool = False) -> subprocess.CompletedProcess[str]:
    command = app.quiet_uninstall_string if quiet and app.quiet_uninstall_string else app.uninstall_string
    if not command:
        raise ValueError("No official uninstall command is registered for this app.")
    return subprocess.run(command, shell=True, check=False, text=True)
