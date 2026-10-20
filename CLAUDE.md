# CLAUDE.md

Guidance for Claude when working in this repository. Keep this file up to date: when a decision, convention, command or milestone changes, update it in the same commit.

## Project

NAMsayin is a guitar rig plugin (VST3 + LV2) in C++20, built with DPF (plugin framework + NanoVG UI) and NeuralAmpModelerCore.

- **Requirements**: `docs/Requirements.md` — the owner's feature list plus agreed decisions. Treat it as the source of truth; record new decisions there.
- **Architecture and milestones**: `docs/Architecture.md` — design, v1 milestones (M0–M10) and decisions log.
- **Current milestone**: M1 (engine core) done — block interface and registry, chain state, lock-free chain swaps, parameter slots, input/tone stack/output/limiter/meters, global and slot bypass, mono/stereo, unit tests. Next: M2 (NAM block).

## Fixed decisions (don't revisit without the owner)

- Formats: VST3 and LV2 only. No standalone app, no CLAP, no AU.
- Platforms: Linux and Windows, x86-64 only, AVX2/FMA required. No macOS, no ARM.
- License: MIT. New source files start with `// SPDX-License-Identifier: MIT` and `// Copyright (c) 2026 singularity098`.
- UI: DPF's NanoVG, dark theme. Draw in logical pixels and scale by `getScaleFactor()`.
- Zero latency by default; only blocks that need latency add it, and it's reported to the host.
- Max 32 blocks; each block keeps a fixed parameter slot for its lifetime (automation/MIDI follow the block).
- One split at a time; branch B may run on a second core.
- v1 scope: local NAM blocks, delay, reverb (room/hall/plate), plus tuner, metronome, presets, settings, undo/redo.
- Primary test DAW: Reaper.

## Layout

```
CMakeLists.txt        Top-level build; builds NAM Core as the static library `nam`
deps/DPF              Submodule (DPF main)
deps/NeuralAmpModelerCore  Submodule, pinned to tag v0.6.0
deps/doctest          Submodule, pinned to v2.4.12 (unit tests)
src/engine/           Audio engine, independent of DPF (static library `namsayin-engine`)
  Params.*            Host parameter indices and global parameter specs
  Block.hpp           Block base class, ParamSpec, BlockDescriptor, Category
  BlockRegistry.*     The list of built-in block types — add new effects here
  Engine.*            Input stage, chain, tone stack, output, limiter, meters; lock-free chain swaps
  SpscQueue.hpp       Lock-free queue (retired graphs go back to the non-RT side through it)
  dsp/                Smoother, Biquad, ToneStack, Limiter, Meter
src/state/            ChainState (the chain as plain data, JSON)
src/plugin/           DistrhoPluginInfo.h + DSP side (Plugin subclass; thin wrapper around Engine)
src/ui/               UI side (UI subclass, NanoVG)
tests/                doctest unit tests; TestSupport.hpp has a test block and an allocation counter
docs/                 Requirements and architecture
deps/khronos/         Vendored glext.h + khrplatform.h (MSVC has none; DPF's download is unreliable)
.github/workflows/    CI: Linux (Ninja) and Windows (MSVC) builds + tests
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

`lv2apply` can't run the plugin (it doesn't support DPF's atom port), so end-to-end checks happen in Reaper.

## Conventions

- DPF style: classes in `START_NAMESPACE_DISTRHO`, members prefixed `f` in DPF subclasses, 4-space indent.
- The audio thread (`run()` and everything it calls) must never allocate, lock or touch files. Heavy work (model loading, chain rebuilds, tuner analysis) happens on other threads and is handed over lock-free.
- The UI may run in a separate process (LV2), so DSP↔UI communication goes through parameters and state only — no direct pointers.
- `CMAKE_CXX_EXTENSIONS` is ON because DPF's UI code uses `typeof`.
- **Host parameter indices are DAW automation IDs — never reorder or remove them.** Globals come first, then 32 slots × 9 (bypass + 8). Parameters added later go after the slots (`kFirstLaterParam` in `src/engine/Params.hpp`).
- Slot parameters are normalized 0..1 for the host; each block's `ParamSpec` maps them to real values.
- Block `typeId`s are stored in presets and plugin state — never rename one.
- Parameter values live in the host. `ChainState.params`/`bypass` are only for presets and undo; the UI pushes them to the host.
- The chain reaches the DSP as the `chain` state string (JSON). `setState()` runs off the audio thread and builds the new graph there.
- New audio-path code gets a test, including the no-allocation check (`testing::AllocationGuard`).

## Notes

- NAM A2 Full/Lite: `nam::SlimmableModel::SetSlimmableSize(1.0)` = Full, `0.0` = Lite (`NAM/slimmable.h`). Not real-time safe — call it off the audio thread.
- Fretbored (the owner's other app, https://github.com/singularity098/fretbored) is the reference for the metronome and tuner features.
- Tone3000's site is blocked by this cloud environment's network policy, so its API docs can't be fetched here.
