# Changelog

All notable changes to Clean-n-Adapt are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions follow
[Semantic Versioning](https://semver.org/).

## [Unreleased]

## [3.0.1] - 2026-11-03

### Changed
- Unified the native report, cleanup-review, and confirmation windows with the
  widget's light/dark theme, accent colour, typography, spacing, and Windows
  11 window chrome.
- Reports now stay inside a themed read-only window with a Copy action instead
  of opening as an unstyled edit control.
- Cleanup review now adapts its columns to the window, displays a live selected
  item and size summary, and presents protected copies as unavailable.

### Fixed
- Protected review candidates, including duplicate keepers, cannot be toggled
  in the UI; they remain visible for inspection but cannot be staged.
- Debloat reversal and empty-selection notifications now use the same native
  confirmation surface as the rest of the widget.

## [3.0.0] - 2026-11-03

### Added
- Native C++17 `cna.exe` maintenance engine and `cna-widget.exe` desktop/tray
  controller, replacing the public Qt/Python implementation released as v2.
- Cache profiles, verified duplicate discovery, quarantine and recovery,
  remnant inspection, `explain`, offline rule/model advice, reversible
  debloat operations, and scheduled maintenance.
- A native, scrollable review window for leftovers, recommendations, and
  duplicate copies. It exposes every parsed candidate with path, category,
  size, confidence, and reason; it supports Select all, Clear all, Inspect,
  and staging a selected set for final confirmation.
- Selective quarantine through repeated `--select PATH` flags for remnants,
  recommendations, and duplicate copies.
- An elevated NSIS installer with Program Files installation, Apps & Features
  registration, upgrade support, Start Menu entries, and an uninstaller.
- CI validation that checks `VERSION`, CLI reporting, installer naming, and
  release-tag/version agreement.

### Changed
- The widget keeps the last scan for each cleanup profile during its session,
  opens text details inside the application, and uses light translucency.
- The tray exposes Store-app inspection rather than an irreversible one-click
  removal command.
- The app uses `VERSION` as the canonical version source for CMake, Windows
  resources, the CLI, installer, and release artifact name.

### Fixed
- Empty folders, shared/vendor working folders, and protected Windows working
  locations are no longer promoted as high-confidence generic leftovers.
- Partially sampled large folders have reduced leftover confidence.
- Installer staging now verifies the binaries passed to NSIS before packaging.

## [2.0.0] - 2026-06-21

Published GitHub release: [v2.0.0](https://github.com/snehil-pandey/clean-n-adapt/releases/tag/v2.0.0).

### Added
- PySide6/Qt desktop dashboard, widget-style cleanup/boost screens, tray
  actions, limited `clean`/`boost`/`refresh` CLI surface, Nuitka build scripts,
  and an Inno Setup installer script, as recorded in the published release
  notes.

## Historical releases

The following published releases are preserved exactly as tagged. Their full
release notes were not reconstructed during the native-codebase migration, so
this changelog deliberately does not infer features beyond their Git history.

- `v1.2.2` - 2026-06-12
- `v1.2.1` - 2026-06-12
- `v1.2.0` - 2026-06-11
- `v1.1.2` - tag preserved; publication date requires GitHub release metadata
- `v1.1.1` - tag preserved; publication date requires GitHub release metadata
- `v1.1.0` - tag preserved; publication date requires GitHub release metadata
- `v1.0.0` - tag preserved; publication date requires GitHub release metadata

[Unreleased]: https://github.com/snehil-pandey/clean-n-adapt/compare/v3.0.0...HEAD
[3.0.0]: https://github.com/snehil-pandey/clean-n-adapt/releases/tag/v3.0.0
[2.0.0]: https://github.com/snehil-pandey/clean-n-adapt/releases/tag/v2.0.0
