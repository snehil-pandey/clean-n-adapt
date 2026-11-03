# Releasing Clean-n-Adapt

`VERSION` is the single authoritative application version. It must contain a
Semantic Versioning value: `MAJOR.MINOR.PATCH`.

The CMake configuration reads it to generate the Windows resource metadata,
the CLI reports it through `cna version`, and the NSIS installer uses it in the
setup filename. Do not put a Git SHA or build number in `VERSION`.

## Version policy

- Increment `MAJOR` for incompatible public behavior or interface changes.
- Increment `MINOR` for backward-compatible features.
- Increment `PATCH` for backward-compatible fixes and maintenance work.

Published tags are immutable. A release tag is always `v` followed by the
exact contents of `VERSION`, such as `v3.0.0`.

## Release checklist

1. Review `CHANGELOG.md` and move the intended changes from `Unreleased` into
   a dated version section.
2. Set `VERSION` to the intended SemVer value and build locally.
3. Run focused tests for every changed subsystem. Never run destructive tests
   on the production profile or installed application.
4. Push the release commit and wait for the Windows CI run to pass.
5. Confirm CI reports the same `cna <VERSION>` value and uploads
   `Clean-n-Adapt-Setup-<VERSION>.exe`.
6. Create an annotated `v<VERSION>` tag on that verified commit and push it.
7. Wait for the tag CI run to pass; CI rejects a tag that does not equal the
   version embedded in the build.
8. Create the GitHub Release from that tag, attach the setup EXE from the
   successful tag run, and use the matching changelog section as release notes.
9. Install the release on a non-production test profile before claiming an
   upgrade path is verified.

Development builds are not releases. They may be built from `main`, but they
must not receive a version tag or a stable GitHub Release until the checklist
has passed.
