# nvwii development and release notes

## Project model

nvwii is intentionally maintained as a focused downstream distribution of Dolphin rather than as a blind rename. The reset point is the current upstream Dolphin master revision used for the release cycle, with nvwii-specific presentation, defaults, packaging, and release automation layered on top.

## Desktop identity

The desktop application is presented as **nvwii**. The emulator keeps the internal target name used by Dolphin's source tree where that minimizes compatibility risk, while Windows/macOS output names, application metadata, user directories, and branding are nvwii-specific.

The custom logo is stored at:

`Data/Sys/Resources/nvwii_logo.svg`

The SVG recreates the supplied red-and-white nvwii mark as a resolution-independent asset.

## UI

The Qt frontend adds:

- a flat dark theme with a red accent;
- an nvwii header panel above the library;
- quick actions for Open Game, Graphics, Controllers, and Settings;
- nvwii-specific application title and icon;
- updated About text linking to the nvwii project.

No gradients are used by the nvwii stylesheet.

## Release policy

GitHub Actions builds:

- Windows x64 portable artifacts;
- macOS arm64 artifacts;
- macOS x86_64 artifacts.

Tagged versions create normal GitHub releases. The scheduled nightly pipeline creates/updates a prerelease named `nvwii Nightly`.

The workflows intentionally package the already-built CMake output rather than attempting an extra dependency-heavy installer step. This keeps the release path predictable and makes artifacts useful for testing.

## Emulator-core direction

The upstream reset brings in current upstream accuracy and compatibility work. nvwii's longer-term core work should be isolated into small, testable changes in:

- Core/CPU and interpreter/JIT correctness;
- VideoCommon backend correctness and frame pacing;
- Wii IOS/HLE behavior;
- audio timing;
- controller/motion input latency;
- shader compilation and pipeline caching.

Core changes should include focused tests or reproducible game-level evidence before being enabled by default.
