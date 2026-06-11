from __future__ import annotations

from pathlib import Path
from typing import Iterable

from .models import Target
from .system import env_path, safe_exists, safe_is_dir, safe_iterdir


def unique_targets(targets: Iterable[Target]) -> list[Target]:
    seen: set[tuple[str, str]] = set()
    result: list[Target] = []
    for target in targets:
        key = (str(target.path).casefold(), target.pattern.casefold())
        if key not in seen:
            seen.add(key)
            result.append(target)
    return result


def list_chromium_profiles(root: Path) -> list[Path]:
    if not safe_exists(root):
        return []
    profiles: list[Path] = []
    for child in safe_iterdir(root):
        if safe_is_dir(child) and (child.name == "Default" or child.name.startswith("Profile ")):
            profiles.append(child)
    return profiles


def build_targets(include_admin: bool = False) -> list[Target]:
    user_profile = env_path("USERPROFILE") or Path.home()
    local = env_path("LOCALAPPDATA") or user_profile / "AppData" / "Local"
    roaming = env_path("APPDATA") or user_profile / "AppData" / "Roaming"
    program_data = env_path("ProgramData") or Path("C:/ProgramData")
    windir = env_path("WINDIR") or Path("C:/Windows")
    temp = env_path("TEMP")
    tmp = env_path("TMP")

    targets: list[Target] = [
        Target("User TEMP", "Temp", temp or local / "Temp"),
        Target("User TMP", "Temp", tmp or local / "Temp"),
        Target("Local Temp", "Temp", local / "Temp"),
        Target("LocalLow Temp", "Temp", user_profile / "AppData" / "LocalLow" / "Temp"),
        Target("Recent Shortcuts", "Windows", roaming / "Microsoft" / "Windows" / "Recent", "*.lnk"),
        Target("Explorer Thumbcache", "Thumbnails", local / "Microsoft" / "Windows" / "Explorer", "thumbcache_*.db"),
        Target("Explorer Iconcache", "Thumbnails", local / "Microsoft" / "Windows" / "Explorer", "iconcache_*.db"),
        Target("Windows WebCache", "Windows", local / "Microsoft" / "Windows" / "WebCache"),
        Target("WER Report Queue", "Windows", program_data / "Microsoft" / "Windows" / "WER" / "ReportQueue"),
        Target("WER Temp", "Windows", program_data / "Microsoft" / "Windows" / "WER" / "Temp"),
        Target("Windows Temp", "System", windir / "Temp", requires_admin=True),
        Target("ProgramData Temp", "System", program_data / "Temp", requires_admin=True),
        Target("Update Download Cache", "System", windir / "SoftwareDistribution" / "Download", requires_admin=True),
        Target("Delivery Optimization", "System", windir / "SoftwareDistribution" / "DeliveryOptimization", requires_admin=True),
        Target("Pip Cache", "Dev", local / "pip" / "Cache"),
        Target("NPM Cache", "Dev", roaming / "npm-cache"),
        Target("Yarn Cache", "Dev", local / "Yarn" / "Cache"),
        Target("Python __pycache__", "Dev", user_profile, "__pycache__", description="Top-level Python cache folders"),
        Target("Pytest Cache", "Dev", user_profile, ".pytest_cache", description="Top-level pytest cache folders"),
        Target("DirectX Shader Cache", "Game", local / "D3DSCache"),
        Target("NVIDIA GLCache", "Game", local / "NVIDIA" / "GLCache"),
        Target("NVIDIA DXCache", "Game", local / "NVIDIA" / "DXCache"),
        Target("AMD Shader Cache", "Game", local / "AMD" / "DxCache"),
        Target("Steam htmlcache", "Game", local / "Steam" / "htmlcache"),
    ]

    chromium_roots = {
        "Chrome": local / "Google" / "Chrome" / "User Data",
        "Edge": local / "Microsoft" / "Edge" / "User Data",
        "Brave": local / "BraveSoftware" / "Brave-Browser" / "User Data",
        "Vivaldi": local / "Vivaldi" / "User Data",
        "Opera": roaming / "Opera Software" / "Opera Stable",
        "Opera GX": roaming / "Opera Software" / "Opera GX Stable",
    }
    chromium_cache_names = [
        "Cache",
        "Code Cache",
        "GPUCache",
        "GrShaderCache",
        "ShaderCache",
        "Media Cache",
        "Service Worker/CacheStorage",
    ]

    for browser, root in chromium_roots.items():
        profile_roots = [root] if browser.startswith("Opera") and safe_exists(root) else list_chromium_profiles(root)
        for profile in profile_roots:
            for cache_name in chromium_cache_names:
                targets.append(Target(f"{browser} {profile.name} {cache_name}", "Browsers", profile / cache_name))

    firefox_profiles = roaming / "Mozilla" / "Firefox" / "Profiles"
    if safe_exists(firefox_profiles):
        for profile in safe_iterdir(firefox_profiles):
            if safe_is_dir(profile):
                targets.extend(
                    [
                        Target(f"Firefox {profile.name} cache2", "Browsers", profile / "cache2"),
                        Target(f"Firefox {profile.name} startupCache", "Browsers", profile / "startupCache"),
                        Target(f"Firefox {profile.name} thumbnails", "Browsers", profile / "thumbnails"),
                    ]
                )

    if not include_admin:
        targets = [target for target in targets if not target.requires_admin]
    return unique_targets(targets)
