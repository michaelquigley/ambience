# CHANGELOG

## Unreleased

FEATURE: **Linux build support.** Ambience now builds and runs on Linux (VST3 + Standalone); upstream targets Windows + MSVC exclusively. There are no changes to the audio engine — the work is build-system portability: a GCC/Clang compile-flag branch beside upstream's MSVC one, and flexible JUCE resolution that tries `-DJUCE_PATH`, then a `$JUCE_PATH` environment variable, then a `../JUCE` sibling, then a FetchContent fallback, so the project builds wherever JUCE lives.

FEATURE: The 21 factory presets are embedded in the binary and extracted to the user's preset folder on first launch, so a freshly-installed plugin shows them without copying a folder by hand. A marker file means presets you delete stay deleted and presets you've edited are never overwritten. On Linux the folder is the XDG location `~/.local/share/Ambience/Presets/`; Documents on Windows/macOS.

FEATURE: The UI is resizable. Clicking the "AMBIENCE" title opens a zoom menu (75%–200%); the chosen scale persists globally (`~/.config/Ambience/scale.settings` on Linux), so new instances open at the last-used size. The layout is bitmap-scaled rather than reflowed, so 100%/150%/200% are pixel-perfect and the in-between scales are slightly soft.

FEATURE: A padlock toggle beside the Wet/Dry knobs locks the mix. While locked, switching presets preserves the current Wet/Dry instead of overwriting it — so the plugin sits cleanly on an aux/send and auditioning presets doesn't re-adjust the mix on every load. The lock state is saved with the session.

CHANGE: Synced to upstream `v1.1.0`, adopting its DSP work — chorus-style pitch modulation, a 3-stage serial allpass chain, Thiran fractional-delay interpolation, the PreDelay fix, long-decay metallic-artifact mitigation, and CPU optimizations — and its version number, while keeping the stable `Ambience` product name and `AMBIENCE` title so existing DAW projects aren't orphaned by a rename. The upstream changelog in `README.md` carries the per-change DSP detail. (https://github.com/OTODESK4193/Ambience1.0.1)

CHANGE: **License is now AGPLv3** (was GPLv3), tracking upstream's relicense at v1.1.0. Anyone redistributing or network-deploying a modified build is bound by AGPL's source-disclosure terms; see the `LICENSE` file.

CHANGE: Continuous integration via GitHub Actions. Every push and pull request builds on `ubuntu-22.04` and statically verifies the result — the `.so` and Standalone link with no missing libraries and all 21 factory presets are confirmed embedded — and uploads the build for that commit. Pushing a `v*` tag drafts a GitHub release with a packaged `Ambience-<tag>-linux-x86_64.zip`.

CHANGE: Added a root `Makefile` wrapping the CMake invocation — `make` (configure + Release build), `make clean`, `make rebuild` — with `BUILD_TYPE`, `JOBS`, and `BUILD_DIR` overridable.
