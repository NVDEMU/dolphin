# nvwii — Modern Wii & GameCube Emulation

![nvwii logo](Data/Sys/Resources/nvwii_logo.svg)

nvwii is a custom, Wii-focused emulator distribution based on the Dolphin emulator project. It keeps the mature GameCube/Wii emulation core while providing a new product identity, modern desktop UI, release infrastructure, and nvwii-specific defaults.

## What is different in nvwii?

- Modern dark desktop interface with a flat, red/charcoal visual system.
- nvwii branding and the supplied logo throughout the application and website.
- Wii-first quick actions for opening games, graphics, settings, and controllers.
- Fresh upstream sync as the starting point for each nvwii development cycle.
- Vulkan-enabled builds and release-oriented compiler settings.
- GitHub Actions for Windows and macOS artifacts.
- Nightly prereleases plus tagged stable releases.
- GitHub Pages website under `website/`.
- nvwii uses its own application/user-data identity instead of reusing the Dolphin user directory.
- Upstream analytics and updater defaults are disabled so nvwii does not silently report telemetry or point users at Dolphin's release service.

## Supported systems

The emulator core retains Dolphin's desktop platform coverage:

- Windows
- macOS
- Linux

Nintendo Wii and GameCube firmware, system files, and game images are not bundled. Use only games and system software you are legally entitled to use.

## Building

Fetch the repository with its submodules:

```sh
git clone --recursive https://github.com/NVDEMU/dolphin.git
cd dolphin
```

### Windows

Use Visual Studio 2022 and CMake. The GitHub Actions release workflow uses the same CMake project and produces a portable `Binaries` package.

### macOS

Use CMake and Qt 6. The release workflow builds a native `.app` bundle and packages it as a zip.

Detailed development notes are in [docs/NVWII_BUILD.md](docs/NVWII_BUILD.md).

## Releases

- Stable: tagged releases such as `v0.1.0`
- Nightly: automatically produced prereleases from the default branch

See the [releases page](https://github.com/NVDEMU/dolphin/releases).

## Website

The static site lives in [website/](website/) and is deployed by GitHub Actions to GitHub Pages.

## License

nvwii is distributed under the same GPLv2-or-later license as the upstream Dolphin code it is derived from. See [COPYING](COPYING) and the [LICENSES](LICENSES/) directory.
