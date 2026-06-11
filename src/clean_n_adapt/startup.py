from __future__ import annotations

import winreg
from dataclasses import dataclass


@dataclass
class StartupEntry:
    name: str
    command: str
    location: str
    hive_name: str = ""
    key_path: str = ""
    value_name: str = ""

    def as_dict(self) -> dict[str, str]:
        return {
            "name": self.name,
            "command": self.command,
            "location": self.location,
            "hive_name": self.hive_name,
            "key_path": self.key_path,
            "value_name": self.value_name,
        }

    def delete(self) -> bool:
        hive = winreg.HKEY_CURRENT_USER if self.hive_name == "HKCU" else winreg.HKEY_LOCAL_MACHINE
        try:
            with winreg.OpenKey(hive, self.key_path, 0, winreg.KEY_SET_VALUE) as key:
                winreg.DeleteValue(key, self.value_name)
            return True
        except OSError:
            return False


RUN_KEYS = [
    (winreg.HKEY_CURRENT_USER, "HKCU", r"Software\Microsoft\Windows\CurrentVersion\Run", "HKCU Run"),
    (winreg.HKEY_LOCAL_MACHINE, "HKLM", r"Software\Microsoft\Windows\CurrentVersion\Run", "HKLM Run"),
    (winreg.HKEY_LOCAL_MACHINE, "HKLM", r"Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Run", "HKLM WOW Run"),
]


def list_startup_entries() -> list[StartupEntry]:
    entries: list[StartupEntry] = []
    for hive, hive_name, path, label in RUN_KEYS:
        try:
            with winreg.OpenKey(hive, path) as key:
                count = winreg.QueryInfoKey(key)[1]
                for index in range(count):
                    try:
                        name, command, _ = winreg.EnumValue(key, index)
                        entries.append(StartupEntry(str(name), str(command), label, hive_name, path, str(name)))
                    except OSError:
                        continue
        except OSError:
            continue
    return sorted(entries, key=lambda item: item.name.casefold())
