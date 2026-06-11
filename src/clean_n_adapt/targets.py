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


DEEP_CACHE_NAMES = {
    ".cache",
    ".parcel-cache",
    ".pytest_cache",
    "__pycache__",
    "cache",
    "cacheddata",
    "caches",
    "cachestorage",
    "code cache",
    "crashpad",
    "d3dscache",
    "dawncache",
    "dxcache",
    "gpcache",
    "gpucache",
    "grshadercache",
    "htmlcache",
    "inetcache",
    "localcache",
    "logs",
    "mediacache",
    "service worker",
    "shader-cache",
    "shadercache",
    "tempstate",
    "temp",
    "tmp",
    "webcache",
}

SKIP_DEEP_NAMES = {
    ".git",
    ".hg",
    ".svn",
    ".venv",
    "env",
    "onedrive",
    "documents",
    "desktop",
    "downloads",
    "pictures",
    "videos",
    "music",
    "node_modules",
    "program files",
    "program files (x86)",
    "runtimes",
    "tools",
    "venv",
    "windows",
}


def deep_cache_targets(root: Path, label: str, category: str, requires_admin: bool = False, max_depth: int = 5) -> list[Target]:
    if not safe_exists(root):
        return []
    targets: list[Target] = []
    stack: list[tuple[Path, int]] = [(root, 0)]
    visited = 0
    while stack and visited < 8000:
        current, depth = stack.pop()
        visited += 1
        current_name = current.name.casefold()
        if depth > 0 and current_name in DEEP_CACHE_NAMES:
            targets.append(Target(f"{label} {current.name}", category, current, requires_admin=requires_admin))
            continue
        if depth >= max_depth:
            continue
        for child in safe_iterdir(current):
            if not safe_is_dir(child):
                continue
            child_name = child.name.casefold()
            if child_name in SKIP_DEEP_NAMES:
                continue
            stack.append((child, depth + 1))
    return targets


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
        Target("Windows INetCache", "Windows", local / "Microsoft" / "Windows" / "INetCache"),
        Target("Windows Temporary Internet Files", "Windows", local / "Microsoft" / "Windows" / "Temporary Internet Files"),
        Target("CrashDumps", "Windows", local / "CrashDumps", "*.dmp"),
        Target("VS Code Cache", "Dev", roaming / "Code" / "Cache"),
        Target("VS Code CachedData", "Dev", roaming / "Code" / "CachedData"),
        Target("VS Code GPUCache", "Dev", roaming / "Code" / "GPUCache"),
        Target("VS Code Logs", "Dev", roaming / "Code" / "logs"),
        Target("Cursor Cache", "Dev", roaming / "Cursor" / "Cache"),
        Target("Cursor CachedData", "Dev", roaming / "Cursor" / "CachedData"),
        Target("Cursor GPUCache", "Dev", roaming / "Cursor" / "GPUCache"),
        Target("Gradle Cache", "Dev", user_profile / ".gradle" / "caches"),
        Target("Maven Repository Cache", "Dev", user_profile / ".m2" / "repository"),
        Target("NuGet Cache", "Dev", local / "NuGet" / "Cache"),
        Target("Go Build Cache", "Dev", local / "go-build"),
        Target("Rust Cargo Registry Cache", "Dev", user_profile / ".cargo" / "registry" / "cache"),
        Target("Discord Cache", "App Cache", roaming / "discord" / "Cache"),
        Target("Discord Code Cache", "App Cache", roaming / "discord" / "Code Cache"),
        Target("Discord GPUCache", "App Cache", roaming / "discord" / "GPUCache"),
        Target("Slack Cache", "App Cache", roaming / "Slack" / "Cache"),
        Target("Slack Code Cache", "App Cache", roaming / "Slack" / "Code Cache"),
        Target("Spotify Browser Cache", "App Cache", local / "Spotify" / "Browser" / "Cache"),
        Target("Teams Cache", "App Cache", roaming / "Microsoft" / "Teams" / "Cache"),
        Target("Teams Code Cache", "App Cache", roaming / "Microsoft" / "Teams" / "Code Cache"),
        Target("Telegram Cache", "App Cache", roaming / "Telegram Desktop" / "tdata" / "user_data" / "cache"),
        Target("WhatsApp Cache", "App Cache", roaming / "WhatsApp" / "Cache"),
        Target("Notion Cache", "App Cache", roaming / "Notion" / "Cache"),
        Target("Figma Cache", "App Cache", roaming / "Figma" / "Cache"),
        Target("Epic Launcher WebCache", "Game", local / "EpicGamesLauncher" / "Saved" / "webcache"),
        Target("Riot Client Cache", "Game", local / "Riot Games" / "Riot Client" / "Data" / "Cache"),
        Target("Battle.net Cache", "Game", program_data / "Battle.net" / "Cache", requires_admin=True),
        Target("Steam ShaderCache", "Game", Path("C:/Program Files (x86)/Steam/steamapps/shadercache"), requires_admin=True),
        Target("Windows Font Cache", "System", windir / "ServiceProfiles" / "LocalService" / "AppData" / "Local" / "FontCache", requires_admin=True),
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

    deep_roots = [
        (local, "LocalAppData", "App Cache", False),
        (roaming, "Roaming", "App Cache", False),
        (user_profile / "AppData" / "LocalLow", "LocalLow", "App Cache", False),
        (user_profile / ".cache", "User .cache", "Dev", False),
        (user_profile / "source", "User source", "Dev", False),
        (user_profile / "repos", "User repos", "Dev", False),
        (user_profile / "Projects", "User Projects", "Dev", False),
        (user_profile / "Documents" / "GitHub", "GitHub projects", "Dev", False),
        (Path("D:/code"), "D code", "Dev", False),
        (Path("D:/Dev/Projects"), "D Dev Projects", "Dev", False),
        (program_data, "ProgramData", "App Cache", True),
    ]
    for root, label, category, needs_admin in deep_roots:
        if not needs_admin or include_admin:
            targets.extend(deep_cache_targets(root, label, category, requires_admin=needs_admin))

    if not include_admin:
        targets = [target for target in targets if not target.requires_admin]
    return unique_targets(targets)
