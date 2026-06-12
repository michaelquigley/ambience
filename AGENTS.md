# AGENTS.md

Guidance for an agent (or developer) working on this fork.

This repository is a fork of the upstream Ambience reverb (`OTODESK4193/Ambience1.0.1`), which targets Windows + MSVC only. The fork lives at `github.com/michaelquigley/ambience` and adds three things on top of the upstream tree:

1. **Linux build support** — CMake portability fixes; no source changes.
2. **Embedded factory presets** — the 21 `.ambpreset` files are baked into the binary and extracted to the user's preset folder on first launch.
3. **Resizable UI** — clicking the "AMBIENCE" title label opens a zoom menu (75% / 100% / 125% / 150% / 175% / 200%).

The fork point is upstream commit `66d669c`. Everything authored by this fork sits in commits on top of it, so the canonical, always-current description of what changed is the git history itself:

```
git log 66d669c..HEAD          # fork commits
git diff 66d669c..HEAD         # the complete fork diff
```

This document explains the *why* behind those commits and how to build and extend the fork. When the code and this file disagree, the code wins — treat the prose here as intent, not as a spec to reapply.

All fork changes are confined to five existing files; there is no new C++ translation unit:

- `CMakeLists.txt`
- `Source/PresetManager.h`, `Source/PresetManager.cpp`
- `Source/PluginEditor.h`, `Source/PluginEditor.cpp`

The upstream codebase is genuinely portable; almost everything below is build-system or behaviour wiring, not C++ porting work.

---

## 1. Build environment (Linux)

Tested on Ubuntu 24.04 with the system toolchain. Other distros that ship a recent GCC and the JUCE Linux dependency list will work identically.

```
sudo apt install build-essential cmake pkg-config \
    libasound2-dev libjack-jackd2-dev \
    libx11-dev libxext-dev libxinerama-dev libxrandr-dev \
    libxcursor-dev libxcomposite-dev libxrender-dev \
    libfreetype-dev libfontconfig-dev \
    libgl-dev \
    libgtk-3-dev libwebkit2gtk-4.1-dev
```

`libcurl` is intentionally not required — `JUCE_USE_CURL=0` is set in `CMakeLists.txt` and we keep it that way. `libwebkit2gtk` is only needed because JUCE's CMake probes for it; the build disables the web browser module via `JUCE_WEB_BROWSER=0`.

CMake 3.22+ and a C++20-capable GCC or Clang. Verified with CMake 3.28 and GCC 13.

### Building

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Artifacts:

- `build/Ambience_artefacts/Release/VST3/Ambience.vst3/Contents/x86_64-linux/Ambience.so`
- `build/Ambience_artefacts/Release/Standalone/Ambience`

Install the VST3 by copying the `Ambience.vst3` bundle directory into `~/.vst3/`, then rescan in your DAW.

### JUCE source resolution

`CMakeLists.txt` resolves JUCE in this order:

1. `${JUCE_PATH}` if you pass `-DJUCE_PATH=/abs/path/to/JUCE`.
2. Otherwise `../JUCE` (sibling directory) on Linux / `C:/JUCE` on Windows.
3. Fall back to `FetchContent` against `juce-framework/JUCE` tag `8.0.10` if neither path exists.

The fallback works with no extra setup but takes ~1 minute of git clone on first configure. For repeat builds, place a clone of JUCE 8.0.x next to the project root so it is reused as a sibling directory.

---

## 2. Linux build support

`CMakeLists.txt` only — no source changes. Windows + MSVC builds are unaffected.

- **Portable JUCE path.** The upstream hardcoded `add_subdirectory(C:/JUCE …)` is replaced by the `JUCE_PATH` cache variable + FetchContent fallback described above.
- **GCC/Clang compile flags.** Upstream's `if(MSVC)` block sets `/arch:AVX2` and Release-only `/O2 /fp:fast`. The codebase has no raw SIMD intrinsics — `/arch:AVX2` is only a hint to the auto-vectorizer — so an `else()` branch adds the matching `-mavx2 -mfma` plus Release `-O3 -ffast-math`.

Things deliberately left alone:

- `Source/PresetManager.cpp` uses `juce::File::userDocumentsDirectory`. On Linux JUCE maps this to `$XDG_DOCUMENTS_DIR` (fallback `~/Documents`), so the preset folder becomes `~/Documents/Ambience/Presets/`. No code change needed.
- `Source/DSP/EarlyReflections.{cpp,h}` and `Source/DSP/SAPFStage.{cpp,h}` exist on disk but are not in `target_sources()` and are not `#include`d anywhere. They are dead in upstream too; not a Linux issue.

---

## 3. Embedded factory presets

Goal: a freshly-installed `.vst3` shows the 21 factory presets without the user having to copy a folder by hand.

Mechanism:

1. `CMakeLists.txt` bundles `Presets/*.ambpreset` into a JUCE `BinaryData` target via `juce_add_binary_data` (namespace `FactoryPresets`, header `FactoryPresets.h`) and links it into `Ambience`. The glob uses `CONFIGURE_DEPENDS` so adding a preset file triggers a reconfigure.
2. `PresetManager`'s constructor calls `seedFactoryPresetsIfNeeded()`, which extracts each embedded preset to the user folder unless a marker file (`.factory_installed_v1`) is present, then writes the marker.

The marker scheme gives two guarantees:

- A preset the user deleted does not silently come back on the next launch.
- A preset the user overwrote (saved under the same name) is preserved, because the loop also skips any output file that already exists.

Implementation notes:

- `FactoryPresets::getNamedResourceOriginalFilename()` returns the original filename with spaces preserved (e.g. `"Vienna Musikverein.ambpreset"`), which is what the preset browser expects. `namedResourceList[]` holds sanitized C++ identifiers — do **not** use those for the output filename.
- `getPresetsFolder()` already creates the folder if missing (upstream behaviour), so the seed needs no extra `createDirectory` call.

To re-seed (for testing, or after bumping the marker name):

```
rm ~/Documents/Ambience/Presets/.factory_installed_v1
```

To ship a future cohort of new factory presets without overwriting any user files, bump the marker name (e.g. `_v2`). Existing presets stay; only the missing ones are extracted.

---

## 4. Resizable UI

Goal: let users pick a UI scale (75%–200%) without rewriting the editor's absolute-pixel layout.

Mechanism: `resized()` still lays everything out in design space (900×540); a uniform `juce::AffineTransform::scale(s)` is then applied to every direct child, and to the `Graphics` context in `paint()`. The editor's outer size is set to `(W * s, H * s)` so the host window adopts the scaled size. The relevant methods are `mouseDown`, `setEditorScale`, and `showScaleMenu` in `Source/PluginEditor.cpp`.

UX choice: the menu is triggered by **left-clicking the "AMBIENCE" title label** in the upper-left (cursor changes to a pointing hand; a tooltip explains it). We tried a global right-click first, but Reaper on Linux swallows the event before it reaches the plugin in some configurations, and the editor's children cover most of the surface anyway, so right-click targets are scarce. A clickable title label is reliable across hosts and easy to discover.

Caveats this design accepts:

- At non-integer scales (125%, 175%) text and the RT60 / decay visualizers are bitmap-scaled, so slightly blurry. 100%, 150%, 200% are pixel-perfect for most widgets.
- Mouse hit detection is correct at all scales — JUCE inverts the transform.
- The scale is **not persisted** across DAW sessions in this fork (see §6).

---

## 5. Verification checklist

After a build on Linux, the following should hold:

- [ ] `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` succeeds.
- [ ] `cmake --build build -j$(nproc)` succeeds.
- [ ] `build/Ambience_artefacts/Release/VST3/Ambience.vst3/Contents/x86_64-linux/Ambience.so` exists (~5.5 MB).
- [ ] `strings` on the `.so` shows the 21 original preset filenames (e.g. `Vienna Musikverein.ambpreset`).
- [ ] `~/Documents/Ambience/Presets/.factory_installed_v1` is created on the first run of the standalone or the VST3 in a host.
- [ ] All 21 `.ambpreset` files appear in that folder on first run.
- [ ] In a host (Reaper, Bitwig, Ardour) the preset browser shows the 21 factory entries.
- [ ] Clicking "AMBIENCE" in the upper-left opens a "UI Scale" menu with the current scale ticked.
- [ ] Picking a different scale resizes the window and repositions every widget; the gradient background and separators repaint to match.

Smoke test (no display required, exits via SIGTERM):

```
timeout 3 build/Ambience_artefacts/Release/Standalone/Ambience
# exit 143 is normal — that is the timeout killing the GUI cleanly.
```

---

## 6. Follow-ups (not bugs)

Well-defined next steps, deliberately out of scope so far:

- **Persist the UI scale across DAW sessions.** Add a `juce::PropertiesFile` read in the editor constructor and a write in `setEditorScale()` (~5 lines). Store at `~/.config/Ambience/scale.settings` on Linux; JUCE picks the right path on Windows/macOS via `PropertiesFile::Options::folderName`.
- **True layout reflow.** This fork bitmap-scales the UI. For crisp text at every scale, the absolute-pixel constants at the top of `PluginEditor.cpp` (`Y_HEADER`, `SEC_TIME`, `KNOB_W`, …) would need to become fractions-of-bounds, and the visualizer paint code would need rewriting — an order of magnitude more work than what was done.
- **macOS build.** Should mostly "just work" now that the Linux port removed the Windows assumptions, but the GCC/Clang `else()` branch adds AVX2 flags unconditionally, which breaks on Apple Silicon. Guard with `if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64)")`.
- **LV2 and CLAP formats.** JUCE 8 supports both. Adding `LV2 CLAP` to the `FORMATS` line of `juce_add_plugin` would be enough; CLAP also needs the `clap-juce-extensions` submodule.

---

## 7. Where each feature lives

| File | Feature(s) |
|---|---|
| `CMakeLists.txt` | Portable JUCE path + FetchContent fallback; GCC/Clang flag branch; embedded-presets `juce_add_binary_data` target and link |
| `Source/PresetManager.h` | Declares `seedFactoryPresetsIfNeeded()` |
| `Source/PresetManager.cpp` | Includes `FactoryPresets.h`; calls the seed from the ctor; implements seed-with-marker |
| `Source/PluginEditor.h` | `mouseDown` override; `scale` member; `setEditorScale` / `showScaleMenu` declarations |
| `Source/PluginEditor.cpp` | Scale-aware `setSize`; title-label cursor/tooltip/listener; `g.addTransform` in `paint()`; child `setTransform` loop in `resized()`; `mouseDown` / `setEditorScale` / `showScaleMenu` |
| `Presets/*.ambpreset` | Unchanged; bundled into the binary by the CMake target above |
| `Source/DSP/EarlyReflections.{cpp,h}`, `Source/DSP/SAPFStage.{cpp,h}` | Unchanged; dead in upstream too |
