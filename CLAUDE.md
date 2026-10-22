# CLAUDE.md

Guidance for Claude when working in this repository. Keep this file up to date: when a decision, convention, command or milestone changes, update it in the same commit.

## Project

NAMsayin is a guitar rig plugin (VST3 + LV2) in C++20, built with DPF (plugin framework + NanoVG UI) and NeuralAmpModelerCore.

- **Requirements**: `docs/Requirements.md` — the owner's feature list plus agreed decisions. Treat it as the source of truth; record new decisions there.
- **Architecture and milestones**: `docs/Architecture.md` — design, v1 milestones (M0–M10) and decisions log.
- **Current milestone**: M1, M2 (minus resampling), M3 (delay, reverb, compressor), M5 (library, settings), M6 (split/merge), M7 (presets, undo/redo, MIDI learn), M8 (tuner) and M9 (metronome) are done, and M4 is done except real block images (the owner is providing them; the Doubler has one). The IR loader (local .wav cab IRs) and the Doppelganger Delay (echoes alternate flanger/chorus; `docs/Architecture.md` 4.7b) are also done, at the owner's request, and so is the pedal set (`docs/Requirements.md` "Pedals", Architecture 4.11; owner's call: no rush to v1). Next: M10 (v1.0 release: performance, DAW testing, docs).

## Fixed decisions (don't revisit without the owner)

- Formats: VST3 and LV2 only. No standalone app, no CLAP, no AU.
- Platforms: Linux and Windows, x86-64 only, AVX2/FMA required. No macOS, no ARM.
- License: MIT. New source files start with `// SPDX-License-Identifier: MIT` and `// Copyright (c) 2026 singularity098`.
- UI: DPF's NanoVG, very dark theme: pure black window, flat near-black gray bars and panels (no gradient, owner's request), purple accent that will become configurable. Theme tokens are at the top of `src/ui/NAMsayinUI.cpp`. Draw in logical pixels and scale by `getScaleFactor()`.
- Block panel: a large block image on the left (drawn placeholder art until real images exist), generic controls right-justified. The generic knobs may later be replaced by knobs on the image itself.
- Zero latency by default; only blocks that need latency add it, and it's reported to the host.
- Max 32 blocks; each block keeps a fixed parameter slot for its lifetime (automation/MIDI follow the block).
- One split at a time; branch B may run on a second core.
- v1 scope: local NAM blocks, local IR blocks, delay, Doppelganger Delay, reverb (room/hall/plate), compressor (studio style), plus tuner, metronome, presets, settings, undo/redo.
- Primary test DAW: Reaper.

## Layout

```
CMakeLists.txt        Top-level build; builds NAM Core as the static library `nam`
deps/DPF              Submodule (DPF main), patched at configure time from patches/dpf/ (Linux only; see its README)
deps/NeuralAmpModelerCore  Submodule, pinned to tag v0.6.0
deps/doctest          Submodule, pinned to v2.4.12 (unit tests)
deps/FFTConvolver     Submodule (MIT): zero-latency partitioned convolution for the IR block
deps/miniz            Submodule, pinned to 3.1.0 (MIT): reading zip files in the library
deps/xxHash           Submodule, pinned to v0.8.3 (BSD-2): XXH3-128 content hashes
src/engine/           Audio engine, independent of DPF (static library `namsayin-engine`)
  Params.*            Host parameter indices and global parameter specs
  Block.hpp           Block base class, ParamSpec, BlockDescriptor, Category
  BlockRegistry.*     The list of built-in block types — add new effects here
  TempoSync.*         Note values for tempo-synced knobs (delay time, LFO rates)
  Engine.*            Input stage, chain (with split/merge), tone stack, output, limiter, meters; lock-free chain swaps
  ParallelWorker.*    Thread that runs split branch B on a second core
  SpscQueue.hpp       Lock-free queue (retired graphs go back to the non-RT side through it)
  dsp/                Smoother, Biquad, ToneStack, Limiter, Meter, Lfo (+ RandomLfo), Svf (+ OnePole, EnvelopeFollower),
                      Waveshapers (clipping curves), PitchShifter (delay-line voices), Fdn (8-line reverb tank),
                      PitchTracker (YIN, real time; synth, harmonizer, octave), PeriodFinder (autocorrelation;
                      pitch-synchronous splices for the pitch shifter, chords included)
                      Oversampler (polyphase IIR half-band, 4x, zero latency)
  dsp/DelayLine.hpp   Delay line with Hermite reads, sine LFO, dry/wet mix law; AlignmentDelay (split branches)
  tuner/              PitchDetector (MPM + sub-cent refinement), Tuner (ring buffer from the audio thread, analysis off it)
  metronome/          Metronome (sample-accurate, free and DAW sync), ClickSamples.cpp (generated)
  blocks/             Block implementations (NAM, IR, delay, Doppelganger delay, reverb, compressor, gate, EQs, chorus, flanger, phaser,
                      tremolo, doubler, wah, auto wah, boost, overdrive, distortion, fuzz, bitcrusher, volume,
                      auto swell, ring mod, rotary, octave, pitch shifter, whammy, poly octave, spring reverb,
                      shimmer, freeze, tape echo, ducking delay, synth, looper, harmonizer, filter)
src/state/            Plain data shared by DSP and UI, no DPF:
  ChainState          the chain (JSON; the split is a block range + per-block branch tag)
  ChainExchange       chain file route for hosts that drop UI state messages
  Paths               per-user config/cache dirs, UTF-8 path conversion, atomic file writes
  Settings            global settings and favorites (one JSON file per user)
  Library             folder/zip scanning, index cache, hashes, sidecar images, zip extraction
  LibraryScanner      background scan thread used by the UI
  Preset              .namsayin preset files (zip: preset.json + embedded files), missing-file resolution
  UndoStack           undo/redo of chain snapshots
src/plugin/           DistrhoPluginInfo.h + DSP side (Plugin subclass; thin wrapper around Engine)
src/ui/               UI side (UI subclass, NanoVG). FolderPicker: the system folder dialog (DPF's only picks files). Immediate-mode: draw functions record hit areas, mouse handlers look them up.
                      Tuner and metronome panels are overlays over the chain. OpenUrl.cpp keeps <windows.h> and Vsync.cpp keeps
                      GLX/Xlib out of the DPF code
                      BlockImages.cpp (generated) holds the built-in block images; ParamHelp.cpp the hover help texts
tests/                doctest unit tests; TestSupport.hpp has a test block and an allocation counter
docs/                 Requirements and architecture
resources/sounds/     Classic metronome samples from Fretbored; tools/embed_sounds.py turns them into ClickSamples.cpp
resources/blocks/     Built-in block images, named <typeId>.<ext>; tools/embed_block_images.py turns them into BlockImages.cpp
deps/khronos/         Vendored glext.h + khrplatform.h (MSVC has none; DPF's download is unreliable)
.github/workflows/    CI: Linux (Ninja) and Windows (MSVC) builds + tests
.github/scripts/      CI helper scripts
patches/dpf/          Our DPF patches (VST3: X11 events wake the UI at once), applied by CMake
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

- **Git: work directly on `main` until v1.0** (the owner's decision: Claude writes nearly all the code, so branches and PRs add little). Commit to `main` and push `main`, even when a session is set up with its own feature branch; no PRs unless the owner asks. Run the tests before every push, since there's no PR CI gate. Revisit after v1.0.

- DPF style: classes in `START_NAMESPACE_DISTRHO`, members prefixed `f` in DPF subclasses, 4-space indent.
- The audio thread (`run()` and everything it calls) must never allocate, lock or touch files. Heavy work (model loading, chain rebuilds, tuner analysis) happens on other threads and is handed over lock-free.
- The UI may run in a separate process (LV2), so DSP↔UI communication goes through parameters and state only — no direct pointers.
- `CMAKE_CXX_EXTENSIONS` is ON because DPF's UI code uses `typeof`.
- **Host parameter indices are DAW automation IDs — never reorder or remove them.** Globals come first, then 32 slots × 17 (bypass + 16; was 8, raised before release for the Doppelganger Delay — fixed from v1.0 on). Parameters added later go after the slots (`kFirstLaterParam` in `src/engine/Params.hpp`).
- Slot parameters are normalized 0..1 for the host; each block's `ParamSpec` maps them to real values. `Choice` params list their labels in `ParamSpec::choices`; the UI draws them as a button that steps through the choices. `Momentary` params are footswitch actions (the looper's buttons): 1 while held (the UI holds a quick click for 80 ms so the DSP sees it), and the block acts on the 0 -> 1 edge, which also suits MIDI footswitches.
- Blocks get the host tempo through `Block::setTempo()` and can show a level in the UI through `Block::meter()` (one hidden output parameter per slot, `slotMeterParam()`; the descriptor's `meterName` makes the panel show it). `Block::setRigInput()` gives blocks the chain's input (the noise gate's "Rig In" key).
- Tempo sync is generic: a descriptor's `syncParam`/`syncedParam`/`syncedIsRate` make the UI show note values (`TempoSync.hpp`); the block maps the knob itself. While the host plays, `Block::setSongPosition()` gives the position in quarter notes; synced LFOs lock to it (`Lfo::syncTo`), so they land on the beat.
- A `Choice` parameter's list must never grow or shrink once released: its normalized values would map to other choices in saved projects. Add a new block type instead.
- Every control needs a hover help text in `src/ui/ParamHelp.cpp` (plain English: what it does and how to use it); `tests/ParamHelpTests.cpp` fails for a block or global control without one. Help is keyed by typeId and control name, so renaming a control means renaming its help entry.
- `tests/BlockSanityTests.cpp` runs every registered block at default, extreme and random settings (finite, bounded, no allocation, silent for silence). New blocks get behaviour tests too (`tests/PedalTests.cpp`).
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
- Metronome: see `docs/Architecture.md` 4.10. All its settings are host parameters (most hidden), not state, so they reach the DSP in every host. After changing `resources/sounds/`, run `python3 tools/embed_sounds.py`; never edit `ClickSamples.cpp` by hand. Digital and Beep are generated in code. Only add samples the owner has cleared for MIT (two Digital samples carrying a third-party artist credit were removed).
- Block images: a block's sidecar image wins; otherwise its type's built-in image from `resources/blocks/<typeId>.*` (any format Pillow reads; embedded as 8-bit PNG, max 480 px high); otherwise the drawn placeholder art. After changing `resources/blocks/`, run `python3 tools/embed_block_images.py` (needs Pillow); never edit `BlockImages.cpp` by hand. First one: the "Doubler" pedal for the Doubler block, added to try the look.
- Tuner: see `docs/Architecture.md` 4.9. The plugin runs `Engine::analyzeTuner()` on its own thread every 20 ms while the tuner is on. Accuracy tests are in `tests/TunerTests.cpp`; keep them passing (±0.1 cent from 31 to 1319 Hz) when touching the detector.
- Split/merge: see `docs/Architecture.md` 4.4. The merge controls are host parameters (after the slots). Parallel processing is the Settings toggle; the DSP reads it when it builds a chain.
- Not done yet in the NAM block: resampling when the host rate differs from the model's training rate.
- IR block: WAV files via NAM Core's `nam::detail::load_wav_ir` (PCM 16/24/32, float 32; channels stored contiguously), linearly resampled to the host rate, energy-normalized, convolved with FFTConvolver's `TwoStageFFTConvolver` (head 64, tail 2048; zero latency). Low/High Cut are off at the ends of their ranges.
- NAM A2 Full/Lite: `nam::SlimmableModel::SetSlimmableSize(1.0)` = Full, `0.0` = Lite (`NAM/slimmable.h`). Not real-time safe — call it off the audio thread.
- Library paths: a file inside a zip is `archive.zip!/inner/path`. Blocks accept these directly: `materializeLibraryFile()` extracts to the cache dir on the non-RT thread. Block configs store `path` and `hash` (XXH3-128).
- Sidecar images must be 8-bit PNG or JPEG: DPF's bundled stb_image can't decode 16-bit PNG.
- Settings live in `~/.config/namsayin/settings.json` (Windows: `%APPDATA%\NAMsayin`); the library index caches are in `~/.cache/namsayin` (`%LOCALAPPDATA%\NAMsayin\Cache`). Every UI polls the settings file once a second, so instances stay in sync. Change settings with `updateSettings()` in the UI, which reloads before saving.
- Text fields (search, folders) need keyboard focus. Some hosts keep keys for themselves; in Reaper, check that typing in the plugin window works (FX window option "Send all keyboard input to plugin" if not).
- MIDI learn: the map (CC → host parameter) is part of the chain state, so it travels like the chain and is in undo and presets. The DSP applies mapped CCs at the start of each buffer and reports every CC (`kParamMidiLastCc/LastValue/CcCount`); the UI learns from those, and also writes mapped values to the host, because LV2 hosts mostly can't take parameter changes from the DSP (VST3 can: `requestParameterValueChange`). The host must route MIDI to the plugin's track.
- Undo covers the chain (blocks, block values, bypass, MIDI map), not the global knobs. Call `beginEdit()` before changing `fChain` in the UI. The UI mirrors host slot values into `fChain` (`syncChainValue`), so presets save automated values too.
- Presets store the global sound controls (input mode, input, bass, middle, treble, output, limiter) and the merge controls by parameter symbol; ones a preset lacks go back to their defaults on load. Embedded files live under `files/<hash>/` in the zip and are extracted to `cache/presets/<hash>/` with their sidecar images.
- DPF's file browser doesn't open in the headless test setup (no desktop portal), so presets were tested there through the Recent list; check Open/Save As in Reaper.
- Fretbored (the owner's other app, https://github.com/singularity098/fretbored) is the reference for the metronome and tuner features (`js/metronome.js`, `js/pitch.js`, `js/tuner.js`).
- Redraw rate: on Linux the UI can only redraw once per host idle tick (pugl processes X11 events and exposes from the host's idle/timer callback; DPF asks VST3 hosts for 16 ms, LV2 hosts choose their own rate, jalv ~30 Hz). A frame costs well under 1 ms of CPU, so drawing isn't the bottleneck. Windows redraws from the native message loop. The owner saw choppy dragging in Reaper on Linux in both LV2 and VST3. DPF leaves the GL swap interval at the driver default (usually vsync on), so each swap could block the host's GUI thread until the next refresh; the UI now turns vsync off for its window (`src/ui/Vsync.cpp`, GLX). Start the host with `NAMSAYIN_SHOW_FPS=1` to show a readout (idle ticks/s, frames/s, worst gap, draw time) to see what limits it. The owner's readout in Reaper: idle 36/s, frames 36/s, worst gap 31 ms, draw 0.17 ms, so Reaper's tick rate is the limit. Fix for VST3: `patches/dpf/0001-vst3-x11-event-handler.patch` registers the X11 connection with the host run loop's event handler, so mouse events are handled and redrawn as they arrive (owner: 70-80 frames/s in Reaper with it, on a 120 Hz monitor; was 36; the timer callback also handles events Xlib queued during its redraws, which changed nothing measurable). With a 1000 Hz mouse (readout: mouse 800-900/s) frames stay ~80/s and the worst gap ~21 ms while draws take 0.17 ms, so Reaper seems to dispatch run-loop fd events at only ~80 Hz; going further would mean not drawing on Reaper's thread at all (a render thread in DPF), which is a large, risky change. The readout also shows mouse motion events/s. LV2 has no such mechanism (only the host's idle calls), so in Reaper the VST3 version is the smooth one.
- Tone3000's site is blocked by this cloud environment's network policy, so its API docs can't be fetched here.
