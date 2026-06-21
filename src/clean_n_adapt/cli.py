from __future__ import annotations

import argparse
import sys

from rich.console import Console

from . import __version__
from .actions import clean_mode, refresh_windows_shell, run_boost


console = Console()


def cmd_clean(args: argparse.Namespace) -> int:
    message, code = clean_mode(mode=args.mode, dry_run=args.dry_run, yes=args.yes)
    console.print(message)
    return code


def cmd_boost(args: argparse.Namespace) -> int:
    message, code = run_boost(args.kind)
    console.print(message)
    return code


def cmd_refresh(_: argparse.Namespace) -> int:
    for message in refresh_windows_shell(include_graphics=True):
        console.print(message)
    return 0


def cmd_gui(_: argparse.Namespace | None = None) -> int:
    from .desktop import launch

    return launch()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="cna",
        description="Clean-n-Adapt desktop app. CLI is intentionally limited to clean, boost, and refresh.",
    )
    parser.add_argument("--version", action="version", version=f"clean-n-adapt {__version__}")
    sub = parser.add_subparsers(dest="command")

    gui = sub.add_parser("gui", help="open the desktop app")
    gui.set_defaults(func=cmd_gui)

    clean = sub.add_parser("clean", help="preview or run cleanup")
    clean.add_argument("--mode", choices=["quick", "safe", "browser", "dev", "gaming", "windows", "full"], default="quick")
    clean.add_argument("--dry-run", action="store_true", default=False, help="preview instead of deleting")
    clean.add_argument("--yes", action="store_true", help="delete without prompting; intended for automation")
    clean.set_defaults(func=cmd_clean)

    boost = sub.add_parser("boost", help="run a boost action")
    boost.add_argument("kind", choices=["dns", "store", "disk", "power", "all"], nargs="?", default="all")
    boost.set_defaults(func=cmd_boost)

    refresh = sub.add_parser("refresh", help="restart explorer.exe and trigger Win+Ctrl+Shift+B")
    refresh.set_defaults(func=cmd_refresh)
    return parser


def main(argv: list[str] | None = None) -> int:
    args_list = sys.argv[1:] if argv is None else argv
    if not args_list:
        return cmd_gui()
    parser = build_parser()
    args = parser.parse_args(args_list)
    if not hasattr(args, "func"):
        parser.print_help()
        return 0
    try:
        return int(args.func(args))
    except KeyboardInterrupt:
        console.print("\n[yellow]Cancelled.[/yellow]")
        return 130
    except Exception as exc:
        console.print(f"[red]Error:[/red] {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
