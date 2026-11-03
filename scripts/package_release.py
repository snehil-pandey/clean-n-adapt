from __future__ import annotations

import argparse
import shutil
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def package(version: str) -> Path:
    required = {
        "cna.exe": ROOT / "dist" / "cna.exe",
        "clean-n-adapt-install.exe": ROOT / "dist" / "clean-n-adapt-install.exe",
        "clean-n-adapt-uninstall.exe": ROOT / "dist" / "clean-n-adapt-uninstall.exe",
    }
    missing = [name for name, path in required.items() if not path.exists()]
    if missing:
        raise FileNotFoundError("Missing release files: " + ", ".join(missing) + ". Run python scripts\\build_exe.py first.")

    package_root = ROOT / "release-package"
    package_dir = package_root / f"clean-n-adapt-v{version}-windows"
    zip_path = package_root / f"clean-n-adapt-v{version}-windows.zip"
    if package_dir.exists():
        shutil.rmtree(package_dir)
    if zip_path.exists():
        zip_path.unlink()
    package_dir.mkdir(parents=True)

    files = [
        *[(path, name) for name, path in required.items()],
        (ROOT / "scripts" / "clean-n-adapt.iss", "clean-n-adapt.iss"),
        (ROOT / "README.md", "README.md"),
        (ROOT / "ARCHITECTURE.md", "ARCHITECTURE.md"),
    ]
    for source, name in files:
        shutil.copy2(source, package_dir / name)

    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in package_dir.rglob("*"):
            archive.write(path, path.relative_to(package_root))
    return zip_path


def main() -> int:
    parser = argparse.ArgumentParser(description="Create the Windows release ZIP.")
    parser.add_argument("--version", default="2.0.0")
    args = parser.parse_args()
    zip_path = package(args.version)
    print(f"Created {zip_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
