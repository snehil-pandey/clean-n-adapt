from __future__ import annotations

import winreg
from dataclasses import dataclass


@dataclass
class StartupEntry:
    name: str
    command: str
    location: str


RUN_KEYS = [
    (winreg.HKEY_CURRENT_USER, r"Software\Microsoft\Windows\CurrentVersion\Run", "HKCU Run"),
    (winreg.HKEY_LOCAL_MACHINE, r"Software\Microsoft\Windows\CurrentVersion\Run", "HKLM Run"),
    (winreg.HKEY_LOCAL_MACHINE, r"Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Run", "HKLM WOW Run"),
]


def list_startup_entries() -> list[StartupEntry]:
    entries: list[StartupEntry] = []
    for hive, path, label in RUN_KEYS:
        try:
            with winreg.OpenKey(hive, path) as key:
                count = winreg.QueryInfoKey(key)[1]
                for index in range(count):
                    try:
                        name, command, _ = winreg.EnumValue(key, index)
                        entries.append(StartupEntry(str(name), str(command), label))
                    except OSError:
                        continue
        except OSError:
            continue
    return sorted(entries, key=lambda item: item.name.casefold())
