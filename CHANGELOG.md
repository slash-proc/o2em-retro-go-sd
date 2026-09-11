# Changelog

This file follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). Release tags must
match a section heading exactly (for example `v1.0.0`).

When you cut a release:

1. Move items from `[Unreleased]` into a new `## [vX.Y.Z] - YYYY-MM-DD` section.
2. Commit the changelog update.
3. Push the tag: `git tag vX.Y.Z && git push origin vX.Y.Z`

CI reads the matching section and uses it as the GitHub Release notes. The tag
is also used in staged asset names (`<binary>-<tag>.bin`, `<binary>-<tag>.zip`).

## [Unreleased]

### Added

- The project adopts the [GWRG distribution model](https://github.com/slash-proc/gwrg-dist-spec).
  A tagged release now carries a `manifest.json` describing what it installs
  and where, an offline bundle holding every file that manifest names, and a
  GitHub Pages mirror a web installer can fetch across origins.
- The shared dist scripts, taken verbatim from the canonical set:
  `make_manifest.py`, `build_dist.py`, `make_bundle.py` and `stage_release.py`.
  They read every project-specific value out of the Makefile, so they stay
  byte-identical across projects and a fix lands everywhere at once.
- `print-SIDECARS` and `print-RO_BIN`, which the staging step reads to find any
  extra device file installed beside the binary. This project has none; the
  targets exist so the shared script needs no per-project variant.
- `gwrg.json` declares what the built bytes cannot: the system's short name,
  that its ROMs are not compressed, and the console BIOS. Any one of
  `o2rom.bin`, `c52.bin`, `g7400.bin` or `jopac.bin` in `/bios/videopac/`
  satisfies it, and which one is present decides the machine model the core
  emulates.

## [v0.0.1]

Initial Videopac/Odyssey 2 core fot Retro-Go SD

### Added

- Nothing.

### Changed

- Nothing.

### Fixed

- Nothing.

### Install

- Unzip the release archive onto the SD card root (`cores/o2em.bin`).
- Place ROMs under `/roms/videopac/` (extension `.bin`).
- BIOS required: `/bios/videopac/o2rom.bin` (or `c52.bin` / `g7400.bin` / `jopac.bin`).
- Requires firmware whose ABI matches `SDK_VERSION` in this repository.
