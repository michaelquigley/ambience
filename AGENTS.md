# AGENTS.md

Guidance for an agent (or developer) working on this fork.

This repository is a fork of the upstream Ambience reverb (`OTODESK4193/Ambience1.0.1`), which targets Windows + MSVC only. The fork lives at `github.com/michaelquigley/ambience` and adds five things on top of the upstream tree:

1. **Linux build support** — CMake portability fixes; no source changes.
2. **Embedded factory presets** — the 21 `.ambpreset` files are baked into the binary and extracted to the user's preset folder on first launch.
3. **Resizable UI** — clicking the "AMBIENCE" title label opens a zoom menu (75% / 100% / 125% / 150% / 175% / 200%). The chosen scale persists globally, so new instances open at the last-used size.
4. **Mix lock** — a padlock toggle by the Wet/Dry knobs; while locked, switching presets preserves the current mix, so the plugin works cleanly on a send.

The fork point is upstream commit `66d669c`. Everything authored by this fork sits in commits on top of it, so the canonical, always-current description of what changed is the git history itself:

```
git log 66d669c..HEAD          # fork commits
git diff 66d669c..HEAD         # the complete fork diff
```

This document explains the *why* behind those commits and how to build and extend the fork. When the code and this file disagree, the code wins — treat the prose here as intent, not as a spec to reapply.

All fork changes are confined to seven existing files; there is no new C++ translation unit:

- `CMakeLists.txt`
- `Source/PresetManager.h`, `Source/PresetManager.cpp`
- `Source/PluginEditor.h`, `Source/PluginEditor.cpp`
- `Source/PluginProcessor.h`, `Source/PluginProcessor.cpp` (mix-lock flag + its session persistence)

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

`CMakeLists.txt` resolves JUCE in this order (first hit that contains a `CMakeLists.txt` wins; otherwise it falls through to FetchContent):

1. `-DJUCE_PATH=/abs/path/to/JUCE` — explicit configure flag, highest priority. Good for one-off builds.
2. `$JUCE_PATH` environment variable — set once in your shell profile (`export JUCE_PATH=/abs/path/to/JUCE`) and every clone finds it regardless of where the repo lives on disk. This is the recommended setup for a JUCE checkout kept in a fixed location.
3. `../JUCE` (sibling directory) on Linux / `C:/JUCE` on Windows — the zero-config fallback when the repo and a JUCE clone sit side by side.
4. `FetchContent` against `juce-framework/JUCE` tag `8.0.10` if none of the above resolves.

The `-D` flag and the env var share the name `JUCE_PATH`, so there is one thing to remember; `-D` wins over the env var because it populates the CMake cache before the env-var default is computed. The resolved source (local path vs. FetchContent) is echoed in the `message(STATUS "JUCE: …")` line at configure time.

FetchContent works with no extra setup but takes ~1 minute of git clone on first configure. For repeat builds, keep a clone of JUCE 8.0.x somewhere stable and point `$JUCE_PATH` at it (or place it next to the project root as a sibling).

---

## 2. Linux build support

`CMakeLists.txt` only — no source changes. Windows + MSVC builds are unaffected.

- **Portable JUCE path.** The upstream hardcoded `add_subdirectory(C:/JUCE …)` is replaced by the `JUCE_PATH` cache variable + FetchContent fallback described above.
- **GCC/Clang compile flags.** Upstream's `if(MSVC)` block sets `/arch:AVX2` and Release-only `/O2 /fp:fast`. The codebase has no raw SIMD intrinsics — `/arch:AVX2` is only a hint to the auto-vectorizer — so an `else()` branch adds the matching `-mavx2 -mfma` plus Release `-O3 -ffast-math`.

Things deliberately left alone:

- `Source/DSP/EarlyReflections.{cpp,h}` and `Source/DSP/SAPFStage.{cpp,h}` exist on disk but are not in `target_sources()` and are not `#include`d anywhere. They are dead in upstream too; not a Linux issue.

(The preset folder location is *not* left alone — see §3. Upstream's `userDocumentsDirectory` misbehaves on Linux when `XDG_DOCUMENTS_DIR` points at `$HOME`.)

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

Preset folder location (`getPresetsFolder()`): upstream uses `juce::File::userDocumentsDirectory` → `Documents/Ambience/Presets/`. That is kept on **Windows/macOS** (a visible, user-managed Documents folder). On **Linux/BSD** it is replaced, because `userDocumentsDirectory` honours `XDG_DOCUMENTS_DIR`, which on some setups is set to `$HOME` (e.g. `XDG_DOCUMENTS_DIR="$HOME/"`) — that would dump an `Ambience/` folder straight into the home directory. So Linux resolves the XDG *data* location instead: `$XDG_DATA_HOME` if set, else its spec default `~/.local/share`, giving **`~/.local/share/Ambience/Presets/`**. JUCE has no special-location enum for `XDG_DATA_HOME`, so it is read by hand. (The scale settings in §4 make the analogous choice with `~/.config`; presets are user *data*, hence `~/.local/share`.)

To re-seed (for testing, or after bumping the marker name) — adjust the path for your platform (Linux shown):

```
rm ~/.local/share/Ambience/Presets/.factory_installed_v1
```

To ship a future cohort of new factory presets without overwriting any user files, bump the marker name (e.g. `_v2`). Existing presets stay; only the missing ones are extracted.

---

## 4. Resizable UI

Goal: let users pick a UI scale (75%–200%) without rewriting the editor's absolute-pixel layout.

Mechanism: `resized()` still lays everything out in design space (900×540); a uniform `juce::AffineTransform::scale(s)` is then applied to every direct child, and to the `Graphics` context in `paint()`. The editor's outer size is set to `(W * s, H * s)` so the host window adopts the scaled size. The relevant methods are `mouseDown`, `setEditorScale`, and `showScaleMenu` in `Source/PluginEditor.cpp`.

UX choice: the menu is triggered by **left-clicking the "AMBIENCE" title label** in the upper-left (cursor changes to a pointing hand; a tooltip explains it). We tried a global right-click first, but Reaper on Linux swallows the event before it reaches the plugin in some configurations, and the editor's children cover most of the surface anyway, so right-click targets are scarce. A clickable title label is reliable across hosts and easy to discover.

Persistence: the scale is stored **globally** (not per-session) in a `juce::PropertiesFile` at `~/.config/Ambience/scale.settings`. The editor reads it in the constructor *before* `setSize`, and `setEditorScale()` writes it on every menu pick. "Global" is deliberate: reopening an old project uses your *current* last-used scale, not whatever scale was active when the project was saved. The `PropertiesFile` is owned by the editor (`uiSettings`), and the options live in the static `uiSettingsOptions()`.

Path gotcha (see `uiSettingsOptions()`): JUCE's Linux path formula for `PropertiesFile` is `File("~").getChildFile(folderName)` — it does **not** prepend `~/.config`. A bare `folderName = "Ambience"` would therefore dump `scale.settings` straight into the home directory (`~/Ambience/…`). So `folderName` is `#if`-guarded: `".config/Ambience"` on Linux/BSD (→ `~/.config/Ambience/scale.settings`), plain `"Ambience"` on Windows/macOS where JUCE already roots it under the app-data / Application-Support directory.

Layout-ordering gotcha (do not remove the explicit `resized()` at the end of the constructor): `setSize()` is called near the top of the constructor, which fires `resized()` *before* any child widgets are built — so its child-`setTransform` loop runs over an empty child set. With a persisted non-100% scale this leaves the children at identity transform unless a later size change happens to fire `resized()` again (which, in the standalone, depends on window-bounds restore and is intermittent — symptom: correctly-sized window but the UI drawn at 100% in the top-left). The constructor therefore calls `resized()` once more after all children exist to guarantee the transform is applied.

Caveats this design accepts:

- At non-integer scales (125%, 175%) text and the RT60 / decay visualizers are bitmap-scaled, so slightly blurry. 100%, 150%, 200% are pixel-perfect for most widgets.
- Mouse hit detection is correct at all scales — JUCE inverts the transform.

---

## 5. Mix lock (lockable send)

Goal: let the user lock the Wet/Dry mix so that auditioning presets — or using the plugin on an aux/send where mix is fixed at 100% wet — does not re-adjust the mix on every preset change.

The key insight is *where* the lock must intervene. A preset load flows:

```
PresetManager::loadPreset(name)
   └─ processor.setStateInformation(data)
        └─ apvts.replaceState(tree)   ← clobbers every parameter, Wet/Dry included
```

`setStateInformation` is **also** the host's session-restore path. So the lock logic must NOT live there, or reopening a saved project would refuse to restore the Wet/Dry stored with it. The interception belongs in `PresetManager::loadPreset` (which prev/next nav also routes through). When locked, it captures the normalized Wet/Dry values before `setStateInformation` and writes them back via `setValueNotifyingHost` after — so the host and the on-screen knobs follow.

The lock flag itself:

- Lives on the processor as a plain `bool mixLocked` (not an APVTS parameter — it is a workflow toggle, not an automatable audio param).
- Persists **per-session**: written in `getStateInformation` and read in `setStateInformation` as a ValueTree property, exactly like `currentPresetName`. (Contrast with the UI scale in §4, which is global — two different scopes for two different kinds of setting.)
- Because it rides in `getStateInformation`, it also lands inside `.ambpreset` files. To stop a preset's stale flag from flipping the live lock, `loadPreset` captures the flag *before* `setStateInformation` and restores it *after*, unconditionally. So a preset load never disturbs the lock; only a genuine host session-restore applies the saved flag.

UI: a small vector-drawn padlock (`MixLockButton`, a `juce::Button` subclass in `PluginEditor.h`) sits just right of the "MIX" section label. Vector, not a PNG, so it stays crisp at every zoom and needs no `BinaryData`/Assets change; it participates in the editor's `setTransform` scale loop like every other child. Locked = closed shackle + Accent colour; unlocked = raised shackle + dim grey. It is visible only in Normal mode (the MIX section is hidden in Pro mode), but the lock is a processor-side flag so it stays in effect regardless of mode. `timerCallback` re-syncs the toggle to `processor.isMixLocked()` so a session-restore-driven change is reflected in the UI.

---

## 6. Verification checklist

After a build on Linux, the following should hold:

- [ ] `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` succeeds.
- [ ] `cmake --build build -j$(nproc)` succeeds.
- [ ] `build/Ambience_artefacts/Release/VST3/Ambience.vst3/Contents/x86_64-linux/Ambience.so` exists (~5.5 MB).
- [ ] `strings` on the `.so` shows the 21 original preset filenames (e.g. `Vienna Musikverein.ambpreset`).
- [ ] `~/.local/share/Ambience/Presets/.factory_installed_v1` is created on the first run of the standalone or the VST3 in a host (Linux; `Documents/Ambience/Presets/` on Windows/macOS).
- [ ] All 21 `.ambpreset` files appear in that folder on first run.
- [ ] In a host (Reaper, Bitwig, Ardour) the preset browser shows the 21 factory entries.
- [ ] Clicking "AMBIENCE" in the upper-left opens a "UI Scale" menu with the current scale ticked.
- [ ] Picking a different scale resizes the window and repositions every widget; the gradient background and separators repaint to match.
- [ ] Picking a scale, then opening a *new* instance: it comes up at that scale. `~/.config/Ambience/scale.settings` exists and holds the value.
- [ ] A padlock toggle sits right of the "MIX" label (Normal mode); it toggles closed/Accent ↔ open/grey on click.
- [ ] With the lock ON, switching presets leaves Wet/Dry untouched while everything else follows the preset. With it OFF, presets set Wet/Dry as before.
- [ ] Lock state survives saving and reopening the host project; loading a preset never flips the lock toggle.

Smoke test (no display required, killed by the timeout):

```
timeout 3 build/Ambience_artefacts/Release/Standalone/Ambience
# A non-zero exit is normal here: 124 is the timeout firing, 143 is the SIGTERM
# it sends — either just means the GUI ran for the full 3s and was killed cleanly.
```

---

## 7. Follow-ups (not bugs)

Well-defined next steps, deliberately out of scope so far:

- **True layout reflow.** This fork bitmap-scales the UI. For crisp text at every scale, the absolute-pixel constants at the top of `PluginEditor.cpp` (`Y_HEADER`, `SEC_TIME`, `KNOB_W`, …) would need to become fractions-of-bounds, and the visualizer paint code would need rewriting — an order of magnitude more work than what was done.
- **macOS build.** Should mostly "just work" now that the Linux port removed the Windows assumptions, but the GCC/Clang `else()` branch adds AVX2 flags unconditionally, which breaks on Apple Silicon. Guard with `if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64)")`.
- **LV2 and CLAP formats.** JUCE 8 supports both. Adding `LV2 CLAP` to the `FORMATS` line of `juce_add_plugin` would be enough; CLAP also needs the `clap-juce-extensions` submodule.

---

## 8. Where each feature lives

| File | Feature(s) |
|---|---|
| `CMakeLists.txt` | Portable JUCE path + FetchContent fallback; GCC/Clang flag branch; embedded-presets `juce_add_binary_data` target and link |
| `Source/PresetManager.h` | Declares `seedFactoryPresetsIfNeeded()` |
| `Source/PresetManager.cpp` | Includes `FactoryPresets.h`; calls the seed from the ctor; implements seed-with-marker; platform-guarded `getPresetsFolder()` (XDG data dir on Linux, Documents on Win/macOS); **mix-lock capture/restore of Wet/Dry + lock flag around `setStateInformation` in `loadPreset`** |
| `Source/PluginProcessor.h` | `mixLocked` flag + `setMixLocked`/`isMixLocked` accessors |
| `Source/PluginProcessor.cpp` | Writes/reads `mixLocked` ValueTree property in `getStateInformation`/`setStateInformation` |
| `Source/PluginEditor.h` | `mouseDown` override; `scale` member; `setEditorScale` / `showScaleMenu` declarations; `uiSettings` `PropertiesFile` + `uiSettingsOptions()`; `MixLockButton` class + `mixLockButton` member |
| `Source/PluginEditor.cpp` | Scale-aware `setSize`; title-label cursor/tooltip/listener; `g.addTransform` in `paint()`; child `setTransform` loop in `resized()`; `mouseDown` / `setEditorScale` / `showScaleMenu`; reads/writes persisted scale; places + wires + sizes `mixLockButton`, re-syncs it in `timerCallback` |
| `Presets/*.ambpreset` | Unchanged; bundled into the binary by the CMake target above |
| `Source/DSP/EarlyReflections.{cpp,h}`, `Source/DSP/SAPFStage.{cpp,h}` | Unchanged; dead in upstream too |
