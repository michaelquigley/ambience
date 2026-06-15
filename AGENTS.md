# AGENTS.md

Guidance for an agent (or developer) working on this fork.

This repository is a fork of the upstream Ambience reverb (`OTODESK4193/Ambience1.0.1`), which targets Windows + MSVC only. The fork lives at `github.com/michaelquigley/ambience` and adds five things on top of the upstream tree:

1. **Linux build support** — CMake portability fixes; no source changes.
2. **Embedded factory presets** — the 21 `.ambpreset` files are baked into the binary and extracted to the user's preset folder on first launch.
3. **Resizable UI** — clicking the "AMBIENCE" title label opens a zoom menu (75% / 100% / 125% / 150% / 175% / 200%). The chosen scale persists globally, so new instances open at the last-used size.
4. **Mix lock** — a padlock toggle by the Wet/Dry knobs; while locked, switching presets preserves the current mix, so the plugin works cleanly on a send.
5. **Continuous integration** — a GitHub Actions workflow builds and statically verifies on every commit (artifacts downloadable per run) and drafts a release on `v*` tags.

The fork branched from upstream `66d669c` and **periodically merges upstream releases** (the process is in §8 "Tracking upstream"). It is currently synced to upstream **v1.1.0**. Because merged upstream commits now live in our history too, a plain `66d669c..HEAD` range no longer isolates fork-authored work — use the file-ownership map in §10 and:

```
git diff upstream/master..HEAD     # the fork's net divergence from the latest synced upstream (our features)
git log --no-merges 66d669c..HEAD  # all commits since the branch (fork-authored + merged-in upstream)
```

This document explains the *why* behind the fork's changes and how to build and extend it. When the code and this file disagree, the code wins — treat the prose here as intent, not as a spec to reapply. The licensing follows upstream: **AGPLv3** (upstream relicensed from GPLv3 at v1.1.0; see the `LICENSE` file).

All **fork-authored** changes are confined to seven existing files plus three new non-source files; there is no new C++ translation unit (upstream's own DSP changes arrive via merge and are not listed here):

- `CMakeLists.txt`
- `Source/PresetManager.h`, `Source/PresetManager.cpp`
- `Source/PluginEditor.h`, `Source/PluginEditor.cpp`
- `Source/PluginProcessor.h`, `Source/PluginProcessor.cpp` (mix-lock flag + its session persistence)
- `.github/workflows/ci.yml` (new — CI/CD, see §7)
- `Makefile` (new — thin CMake wrapper, see §1)
- `CHANGELOG.md` (new — in-house changelog convention, see §8)

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

Or via the root `Makefile`, a thin wrapper over exactly those commands: `make` (configure + Release build), `make clean` (removes `build/`), `make rebuild`. Overridable: `make BUILD_TYPE=Debug`, `make JOBS=8`, `make BUILD_DIR=out`. It passes nothing about JUCE, so the resolution order in "JUCE source resolution" below still applies.

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

**Known non-issue — Standalone relaunch hang (Linux/PipeWire).** The Standalone opens an ALSA device; under PipeWire, quitting and *immediately* relaunching can hang for ~30–60s with **no window** while PipeWire releases the stream the previous instance held. It self-resolves once the device frees (or just wait a bit between launches). It is **not** a plugin bug and is **scale-independent** — do not chase it as a UI-scale issue (we did, at length; it isn't one). The VST3 in a DAW doesn't hit this, and CI never launches the GUI for the same reason (§7). Symptom note: a *correctly-sized* window that renders the UI at 100% in the top-left is a different, fixed bug (the §4 layout-ordering gotcha), not this hang.

---

## 7. Continuous integration

`.github/workflows/ci.yml` (the only file outside the seven C++/CMake files above) runs on GitHub Actions:

- **`build` job** — on every push (any branch), every pull request, and `v*` tags. Runs on `ubuntu-22.04`, chosen deliberately *older* than the 24.04 dev box so the `.so` / Standalone link against an older glibc and run on more distros. Installs the apt deps, configures + builds Release, **statically verifies** the result, and uploads `Ambience.vst3` + the Standalone as a run artifact named with the short commit SHA (so each commit's build is downloadable from its run).
- **`release` job** — only on `v*` tags. `needs: build`; it *downloads* the build job's verified artifact rather than rebuilding (so the released binary is byte-for-byte the one that passed verification), zips it as `Ambience-<tag>-linux-x86_64.zip`, and creates a **draft** GitHub release via `softprops/action-gh-release`.

Things that matter if you touch the workflow:

- **Reduced apt list.** CI installs *fewer* packages than §1: `libgtk-3-dev` and `libwebkit2gtk-*-dev` are dropped. With `JUCE_WEB_BROWSER=0` and no `NEEDS_WEB_BROWSER`, JUCE never requires or links them, so dropping them also sidesteps the 22.04 (`-4.0`) vs 24.04 (`-4.1`) webkit package-name split. (The §1 list is the full local-dev superset and stays correct for a desktop build.)
- **JUCE via FetchContent.** The runner has no sibling checkout or `$JUCE_PATH`, so JUCE resolves to the FetchContent fallback (§1), cached at `build/_deps` keyed on the `8.0.10` tag + a hash of `CMakeLists.txt`.
- **Static verification, not a launch.** The runner is headless and the standalone can hang acquiring an audio device, so CI does **not** run the GUI. It checks: the `.so` and Standalone exist, `ldd` resolves with no "not found", and all 21 embedded preset filenames appear in the `.so`. The preset check dumps `strings -a` to a file and greps the file — do **not** pipe `strings … | grep -Fq`: `-q` SIGPIPEs `strings` and `set -o pipefail` then fails the step *even on a match* (this bit once already).
- **AVX2.** The binary is built with the unconditional `-mavx2 -mfma` (§2), so a released build requires an AVX2 CPU at runtime; the draft-release body says so. GitHub's x86_64 runners have AVX2, so build + verify pass there.
- **Tag vs version.** A `vX.Y.Z` tag names the zip and the release but does **not** change the plugin's internal version, which is hardcoded in `CMakeLists.txt` (three places; currently `1.1.0`) — see the follow-up in §9.

To test the release path without a real release: push a throwaway tag (`git tag v0.0.1-test && git push origin v0.0.1-test`), confirm a draft release with the zip appears, then delete both the draft and the tag (`git push --delete origin v0.0.1-test`).

---

## 8. Tracking upstream

The fork stays mergeable with upstream because it touches a **small, known set of files** and never edits `Source/DSP/*`. Upstream (`OTODESK4193/Ambience1.0.1`) develops on `master` and tags releases (`v1.0.1`, `v1.1.0`, …). Our fork point `66d669c` is a true ancestor of upstream's line, so each sync is a clean three-way **merge** (we do *not* rebase — that would rewrite already-pushed history).

One-time setup:
```
git remote add upstream https://github.com/OTODESK4193/Ambience1.0.1
git config remote.upstream.tagOpt --no-tags   # keep upstream's tags OUT of our tag namespace
```
The `--no-tags` matters: the fork cuts its own releases in the `v*` namespace (the CI `release` job keys on the tag, §7), and upstream uses the same `vX.Y.Z` scheme — fetching upstream's tags into `refs/tags/` would collide with the fork's own release tags. We track `upstream/master` (a branch ref); we do not need upstream's tags locally.

Each sync:
```
1. git fetch upstream          # NOT --tags (see above)
2. review:  git log --oneline HEAD..upstream/master
            git diff <merge-base>..upstream/master -- <fork-owned files>   # confirm disposition
3. git switch -c merge/upstream-<ver> master
4. git merge upstream/master            # clean base; DSP/preset/manual files merge untouched
5. resolve ONLY the fork-owned files (conflict map below)
6. reconcile: adopt upstream's version; keep the "Ambience" product name/`getName()`/title;
   license tracks upstream
7. build + run the static verification (the same checks CI runs, §7)
8. commit the merge; push; let CI confirm
```

**Conflict map** — who owns each file when upstream and the fork both touch it:

| File(s) | Stance on conflict |
|---|---|
| `Source/DSP/*`, `Presets/*`, `Source/Assets/*` (manuals) | **Take upstream** — the fork never edits these; they auto-merge |
| `LICENSE` | **Take upstream** (currently AGPLv3) |
| `CMakeLists.txt` | Keep our build logic (JUCE-path resolution, GCC/Clang flag branch, binary-data target); take upstream's **version** + product **description**; **keep `PRODUCT_NAME "Ambience"`** (reject any `Ambience1.1`-style rename) |
| `Source/PluginProcessor.h` | Keep our `mixLocked` accessors; **keep `getName()` → "Ambience"** |
| `Source/PluginEditor.cpp` | Keep our scale + mix-lock code; **keep title "AMBIENCE"** |
| `Source/PresetManager.cpp` | Keep our seeding/path/mix-lock; integrate upstream hooks additively (e.g. the v1.1.0 promode-reset-on-load) |
| `README.md` | Keep our 🐧 fork banner (top); take upstream's body/badges/changelog |
| `AGENTS.md`, `.github/`, `.gitignore` | **Keep ours** — upstream doesn't touch them |

**Naming decision (recorded):** the fork keeps the stable identifiers `PRODUCT_NAME "Ambience"` / `getName() "Ambience"` / title `"AMBIENCE"` even though upstream moved to `Ambience1.1`-style names — renaming the plugin would orphan existing DAW project references. We *do* adopt upstream's version number (so v1.1.0 features ↔ version 1.1.0; this also keeps CI release tags aligned with the internal version). The DSP is accepted wholesale (it's always-on and interleaved in `UniversalEngine.cpp` — effectively all-or-nothing).

Last sync: **v1.1.0** (`9c6cf10`).

**Changelog.** `CHANGELOG.md` follows the in-house convention (not Keep a Changelog): newest release first, each entry a prose paragraph led by `FEATURE:`, `CHANGE:`, or `FIX:` (those three tags only), ordered most-important-first within a release. New entries are always written into the pinned `## Unreleased` slot — never guess a version. The file itself is the clearest example of the format.

**Cutting a fork release.** The fork tags its own releases in the `v*` namespace; the CI `release` job (§7) keys on the tag and drafts a GitHub release. The fork picks its own version number each release — it need not equal upstream's (the first release happened to align at `v1.1.0` because that's the upstream we synced). To release from `master`:

```
1. land the merge:   git switch master && git merge --ff-only merge/upstream-<ver> && git push
2. bump CHANGELOG.md: rename `## Unreleased` → `## vX.Y.Z`, add a fresh empty `## Unreleased`; commit + push
3. tag + push:        git tag vX.Y.Z && git push origin vX.Y.Z
4. CI builds, statically verifies, packages the zip, and creates a DRAFT release — review it on the
   repo's Releases page (paste the CHANGELOG entries into the body) and publish. It stays a draft until you do.
```

Gotcha: if you ever ran `git fetch upstream --tags` before the `--no-tags` config above, upstream's `vX.Y.Z` tags are sitting in your local `refs/tags/` and will block `git tag vX.Y.Z`. They never reach origin (a plain `git push` doesn't send tags). Clear them with `git tag -d v1.0.1 v1.1.0` (they're upstream's, not the fork's).

---

## 9. Follow-ups (not bugs)

Well-defined next steps, deliberately out of scope so far:

- **True layout reflow.** This fork bitmap-scales the UI. For crisp text at every scale, the absolute-pixel constants at the top of `PluginEditor.cpp` (`Y_HEADER`, `SEC_TIME`, `KNOB_W`, …) would need to become fractions-of-bounds, and the visualizer paint code would need rewriting — an order of magnitude more work than what was done.
- **macOS build.** Should mostly "just work" now that the Linux port removed the Windows assumptions, but the GCC/Clang `else()` branch adds AVX2 flags unconditionally, which breaks on Apple Silicon. Guard with `if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64)")`.
- **LV2 and CLAP formats.** JUCE 8 supports both. Adding `LV2 CLAP` to the `FORMATS` line of `juce_add_plugin` would be enough; CLAP also needs the `clap-juce-extensions` submodule.
- **Reconcile the release tag with the plugin version.** CI (§7) names the release/zip from the `v*` tag, but `CMakeLists.txt` hardcodes `1.0.0` in three places (`project(... VERSION)`, `juce_add_plugin(... VERSION)`, `AMBIENCE_VERSION`), so a tagged build still self-reports `1.0.0`. A CI step could derive the version from the tag and pass it via `-D` (or `sed` it in) before configure.

---

## 10. Where each feature lives

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
| `.github/workflows/ci.yml` | CI/CD: per-commit build + static verification with artifact upload; draft release on `v*` tags (§7) |
| `Makefile` | Thin CMake wrapper: `make` / `make clean` / `make rebuild` (§1) |
| `CHANGELOG.md` | In-house changelog (`FEATURE`/`CHANGE`/`FIX` prose into `## Unreleased`; bumped at release, §8) |
| `Source/DSP/EarlyReflections.{cpp,h}`, `Source/DSP/SAPFStage.{cpp,h}` | Unchanged; dead in upstream too |
