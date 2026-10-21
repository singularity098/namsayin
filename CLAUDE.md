# CLAUDE.md

Guidance for Claude when working in this repository. Keep this file up to date: when a decision, convention, command or milestone changes, update it in the same commit.

## Project

NAMsayin is a guitar rig plugin (VST3 + LV2) in C++20, built with DPF (plugin framework + NanoVG UI) and NeuralAmpModelerCore.

- **Requirements**: `docs/Requirements.md` — the owner's feature list plus agreed decisions. Treat it as the source of truth; record new decisions there.
- **Architecture and milestones**: `docs/Architecture.md` — design, v1 milestones (M0–M10) and decisions log.
- **Current milestone**: M1, M2 (minus resampling), M3 (delay, reverb, compressor) and M5 (library, settings) are done, and M4 is done except real block images (the owner will provide them) and the MIDI learn UI (part of M7). The IR loader (local .wav cab IRs) is also done, at the owner's request. Agreed order from here: M7 (presets, undo/redo, MIDI learn); then M6, M8, M9.

## Fixed decisions (don't revisit without the owner)

- Formats: VST3 and LV2 only. No standalone app, no CLAP, no AU.
- Platforms: Linux and Windows, x86-64 only, AVX2/FMA required. No macOS, no ARM.
- License: MIT. New source files start with `// SPDX-License-Identifier: MIT` and `// Copyright (c) 2026 singularity098`.
- UI: DPF's NanoVG, very dark theme: pure black window, flat near-black gray bars and panels (no gradient, owner's request), purple accent that will become configurable. Theme tokens are at the top of `src/ui/NAMsayinUI.cpp`. Draw in logical pixels and scale by `getScaleFactor()`.
- Block panel: a large block image on the left (drawn placeholder art until real images exist), generic controls right-justified. The generic knobs may later be replaced by knobs on the image itself.
- Zero latency by default; only blocks that need latency add it, and it's reported to the host.
- Max 32 blocks; each block keeps a fixed parameter slot for its lifetime (automation/MIDI follow the block).
- One split at a time; branch B may run on a second core.
- v1 scope: local NAM blocks, local IR blocks, delay, reverb (room/hall/plate), compressor (studio style), plus tuner, metronome, presets, settings, undo/redo.
- Primary test DAW: Reaper.

## Layout

```
CMakeLists.txt        Top-level build; builds NAM Core as the static library `nam`
deps/DPF              Submodule (DPF main)
deps/NeuralAmpModelerCore  Submodule, pinned to tag v0.6.0
deps/doctest          Submodule, pinned to v2.4.12 (unit tests)
deps/FFTConvolver     Submodule (MIT): zero-latency partitioned convolution for the IR block
deps/miniz            Submodule, pinned to 3.1.0 (MIT): reading zip files in the library
deps/xxHash           Submodule, pinned to v0.8.3 (BSD-2): XXH3-128 content hashes
src/engine/           Audio engine, independent of DPF (static library `namsayin-engine`)
  Params.*            Host parameter indices and global parameter specs
  Block.hpp           Block base class, ParamSpec, BlockDescriptor, Category
  BlockRegistry.*     The list of built-in block types — add new effects here
  Engine.*            Input stage, chain, tone stack, output, limiter, meters; lock-free chain swaps
  SpscQueue.hpp       Lock-free queue (retired graphs go back to the non-RT side through it)
  dsp/                Smoother, Biquad, ToneStack, Limiter, Meter
  dsp/DelayLine.hpp   Delay line with Hermite reads, sine LFO, dry/wet mix law
  blocks/             Block implementations (NamBlock, IrBlock, DelayBlock, ReverbBlock, CompressorBlock)
src/state/            Plain data shared by DSP and UI, no DPF:
  ChainState          the chain (JSON)
  ChainExchange       chain file route for hosts that drop UI state messages
  Paths               per-user config/cache dirs, UTF-8 path conversion, atomic file writes
  Settings            global settings and favorites (one JSON file per user)
  Library             folder/zip scanning, index cache, hashes, sidecar images, zip extraction
  LibraryScanner      background scan thread used by the UI
src/plugin/           DistrhoPluginInfo.h + DSP side (Plugin subclass; thin wrapper around Engine)
src/ui/               UI side (UI subclass, NanoVG). Immediate-mode: draw functions record hit areas, mouse handlers look them up
tests/                doctest unit tests; TestSupport.hpp has a test block and an allocation counter
docs/                 Requirements and architecture
deps/khronos/         Vendored glext.h + khrplatform.h (MSVC has none; DPF's download is unreliable)
.github/workflows/    CI: Linux (Ninja) and Windows (MSVC) builds + tests
.github/scripts/      CI helper scripts
```

Planned layout for later milestones is in `docs/Architecture.md` section 2.

## Commands

```sh
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure          # or ./build/namsayin-tests
LV2_PATH=$PWD/build/bin lv2info https://github.com/singularity098/namsayin   # check the LV2 bundle loads
```

Output goes to `build/bin/namsayin.lv2` and `build/bin/namsayin.vst3`.

Sanitizer runs (worth doing after touching threading or the engine):

```sh
cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" -DCMAKE_C_FLAGS="-fsanitize=thread"
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g" -DCMAKE_C_FLAGS="-fsanitize=address,undefined"
cmake --build build-tsan --target namsayin-tests && ./build-tsan/namsayin-tests
```

`lv2apply` can't run the plugin (it doesn't support DPF's atom port). To see and drive the UI headless:

```sh
apt-get install -y xvfb jalv jackd2 imagemagick xdotool
Xvfb :99 -screen 0 1400x900x24 & jackd -d dummy -r 48000 -p 256 &
DISPLAY=:99 LV2_PATH=$PWD/build/bin jalv.gtk3 https://github.com/singularity098/namsayin &
DISPLAY=:99 import -window root shot.png        # screenshot; drive it with xdotool
```

The file browser opens in the host's working directory. Final checks happen in Reaper.

## Conventions

- DPF style: classes in `START_NAMESPACE_DISTRHO`, members prefixed `f` in DPF subclasses, 4-space indent.
- The audio thread (`run()` and everything it calls) must never allocate, lock or touch files. Heavy work (model loading, chain rebuilds, tuner analysis) happens on other threads and is handed over lock-free.
- The UI may run in a separate process (LV2), so DSP↔UI communication goes through parameters and state only — no direct pointers.
- `CMAKE_CXX_EXTENSIONS` is ON because DPF's UI code uses `typeof`.
- **Host parameter indices are DAW automation IDs — never reorder or remove them.** Globals come first, then 32 slots × 9 (bypass + 8). Parameters added later go after the slots (`kFirstLaterParam` in `src/engine/Params.hpp`).
- Slot parameters are normalized 0..1 for the host; each block's `ParamSpec` maps them to real values. `Choice` params list their labels in `ParamSpec::choices`; the UI draws them as a button that steps through the choices.
- Blocks get the host tempo through `Block::setTempo()` and can show a level in the UI through `Block::meter()` (one hidden output parameter per slot, `slotMeterParam()`).
- Block `typeId`s are stored in presets and plugin state — never rename one.
- Parameter values live in the host. `ChainState.params`/`bypass` are only for presets and undo; the UI pushes them to the host.
- The chain reaches the DSP as the `chain` state string (JSON). `setState()` runs off the audio thread and builds the new graph there.
- New audio-path code gets a test, including the no-allocation check (`testing::AllocationGuard`).
- No fast-math (`-ffast-math`, `/fp:fast`) and no NaN sentinels: compilers may assume NaN never occurs. A Windows-only test failure came from exactly this. To check, build the tests with `-DCMAKE_CXX_FLAGS=-ffast-math` and run them.

## Notes

- CI fetches submodules with `.github/scripts/fetch-submodules.sh` (retries; skips NAM Core's unused AudioDSPTools) and caches them, because Eigen is hosted on GitLab, which sometimes refuses requests under load.
- NAM Core is linked as a whole archive: it registers model architectures from static initializers, which a normal static link drops.
- NAM Core is built with `NAM_SAMPLE_FLOAT`, so models process `float` directly.
- NAM tests use the models in `deps/NeuralAmpModelerCore/example_models` (`NAMSAYIN_EXAMPLE_MODELS`).
- The engine reports running slots through two output parameters (`kParamRunningSlotsLow/High`); the UI marks blocks that haven't started 2.5 s after a chain change, and shows a banner if none have.
- **Reaper's LV2 hosting doesn't deliver the UI's state messages to the DSP** (confirmed by the owner; VST3 works). So the chain has a second route: the UI writes it to a per-instance file and bumps the hidden Chain Revision parameter; a DSP thread reads the file (`src/state/ChainExchange.*`). The DSP also keeps the chain itself (`DISTRHO_PLUGIN_WANT_FULL_STATE`) for project save/restore and writes the file so a reopened UI can read it. Test the fallback with a build configured with `-DCMAKE_CXX_FLAGS=-DNAMSAYIN_TEST_NO_STATE_MESSAGE` (UI never sends the state message) in jalv.
- Not done yet in the NAM block: resampling when the host rate differs from the model's training rate.
- IR block: WAV files via NAM Core's `nam::detail::load_wav_ir` (PCM 16/24/32, float 32; channels stored contiguously), linearly resampled to the host rate, energy-normalized, convolved with FFTConvolver's `TwoStageFFTConvolver` (head 64, tail 2048; zero latency). Low/High Cut are off at the ends of their ranges.
- NAM A2 Full/Lite: `nam::SlimmableModel::SetSlimmableSize(1.0)` = Full, `0.0` = Lite (`NAM/slimmable.h`). Not real-time safe — call it off the audio thread.
- Library paths: a file inside a zip is `archive.zip!/inner/path`. Blocks accept these directly: `materializeLibraryFile()` extracts to the cache dir on the non-RT thread. Block configs store `path` and `hash` (XXH3-128).
- Sidecar images must be 8-bit PNG or JPEG: DPF's bundled stb_image can't decode 16-bit PNG.
- Settings live in `~/.config/namsayin/settings.json` (Windows: `%APPDATA%\NAMsayin`); the library index caches are in `~/.cache/namsayin` (`%LOCALAPPDATA%\NAMsayin\Cache`). Every UI polls the settings file once a second, so instances stay in sync. Change settings with `updateSettings()` in the UI, which reloads before saving.
- Text fields (search, folders) need keyboard focus. Some hosts keep keys for themselves; in Reaper, check that typing in the plugin window works (FX window option "Send all keyboard input to plugin" if not).
- Fretbored (the owner's other app, https://github.com/singularity098/fretbored) is the reference for the metronome and tuner features.
- Tone3000's site is blocked by this cloud environment's network policy, so its API docs can't be fetched here.
