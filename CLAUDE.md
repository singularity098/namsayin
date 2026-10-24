# CLAUDE.md

Guidance for Claude when working in this repository. Keep this file up to date: when a decision, convention, command or milestone changes, update it in the same commit.

## Project

NAMsayin is a guitar rig plugin (VST3 + LV2) in C++20, built with DPF (plugin framework + NanoVG UI) and NeuralAmpModelerCore.

- **Requirements**: `docs/Requirements.md` — the owner's feature list plus agreed decisions. Treat it as the source of truth; record new decisions there.
- **Architecture and milestones**: `docs/Architecture.md` — design, v1 milestones (M0–M10) and decisions log.
- **Current milestone**: M1, M2 (minus resampling), M3 (delay, reverb, compressor), M5 (library, settings), M6 (split/merge), M7 (presets, undo/redo, MIDI learn), M8 (tuner) and M9 (metronome) are done, and M4 is done (the owner prefers the neon placeholder art, with a unique symbol per pedal, to images: the Doubler's photo was removed, 2026-10-23). The IR loader (local .wav cab IRs) and the Doppelganger Delay (echoes alternate flanger/chorus; `docs/Architecture.md` 4.7b) are also done, at the owner's request, and so is the pedal set, including the second set of 18, six more synths, the Poly Chip (polyphonic chip synth), the third set of 16 (2026-10-23: Sympathetic Strings to Piano, and a Fart pedal), better note tracking (NoteTracker, Track From, Lookahead; the poly finder too), MIDI Out (2026-10-24) and the Modulation Sweeper (sweeps between 2-3 of chorus/flanger/phaser/vibrato; `docs/Requirements.md` "Pedals", Architecture 4.11; owner's call: no rush to v1). Next: M10 (v1.0 release: performance, DAW testing, docs).

## Fixed decisions (don't revisit without the owner)

- Formats: VST3 and LV2 only. No standalone app, no CLAP, no AU.
- Platforms: Linux and Windows, x86-64 only, AVX2/FMA required. No macOS, no ARM.
- License: MIT. New source files start with `// SPDX-License-Identifier: MIT` and `// Copyright (c) 2026 singularity098`.
- UI: DPF's NanoVG, very dark theme: the window background is the owner's dark wood image (`resources/background/`), only behind the panels; bars, panels, tiles, buttons and dialogs are black with hairline gray outlines (no gradient; owner's choice), blue accent by default (configurable in Settings, where blue is the first swatch; was purple), hairline icons. Font: Jost Light. Knobs: tick scale lit to the value, with a dot for the position. Theme tokens are at the top of `src/ui/NAMsayinUI.cpp`. Draw in logical pixels and scale by `getScaleFactor()`.
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
  Engine.*            Input stage, chain (with split/merge), tone stack, output, limiter, meters; lock-free chain swaps;
                      the MIDI path (blocks' MIDI timed past later blocks' latency, queued, note-offs when a block goes)
  MidiOutput.hpp      What blocks write MIDI into (fixed buffer), and the per-slot record of sounding notes
  ParallelWorker.*    Thread that runs split branch B on a second core
  SpscQueue.hpp       Lock-free queue (retired graphs go back to the non-RT side through it)
  dsp/                Smoother, Biquad, ToneStack, Limiter, Meter, Lfo (+ RandomLfo), Svf (+ OnePole, EnvelopeFollower),
                      Waveshapers (clipping curves), PitchShifter (delay-line voices), Fdn (8-line reverb tank),
                      PitchTracker (YIN, real time), NoteTracker (notes from it: starts, ends, velocity, pick filter,
                      lookahead; synths, harmonizer, octave, MIDI Out), PolyNoteFinder (chords: semitone filter bank;
                      Poly Chip, MIDI Out Poly), PickFilter (old pick filter, kept for its tests and the benchmark),
                      PeriodFinder (autocorrelation; pitch-synchronous splices for the pitch shifter, chords included)
                      Oversampler (polyphase IIR half-band, 4x, zero latency), OnsetDetector (new pick attacks),
                      Oscillator (PolyBLEP saw and pulse for the synths)
  dsp/DelayLine.hpp   Delay line with Hermite reads, sine LFO, dry/wet mix law; AlignmentDelay (split branches)
  tuner/              PitchDetector (MPM + sub-cent refinement), Tuner (ring buffer from the audio thread, analysis off it)
  metronome/          Metronome (sample-accurate, free and DAW sync; all sounds generated in code)
  blocks/TrackedInput.hpp  Track From (Block In / Rig In) and Lookahead around the NoteTracker, for the pitch-following blocks
  blocks/             Block implementations (NAM, IR, delay, Doppelganger delay, reverb, compressor, gate, EQs, chorus, flanger, phaser,
                      modulation sweeper,
                      tremolo, doubler, wah, auto wah, boost, overdrive, distortion, fuzz, bitcrusher, volume,
                      auto swell, ring mod, rotary, octave, pitch shifter, whammy, poly octave, spring reverb,
                      shimmer, freeze, tape echo, ducking delay, synth, looper, harmonizer, filter; second set: gated fuzz,
                      envelope drive, kill switch, barber pole, warble, slicer, bloom and gated reverb, reverse and pitch
                      delay, beat repeat, detune, capo, arpeggiator, organ, sustainer, robot filter, talk box;
                      SynthBlocks: super saw, FM, fold, acid, string machine and chip synths;
                      PolyChipBlock: polyphonic chip synth from a semitone filter bank; third set: ResonatorBlocks
                      (sympathetic strings, jawari), CloudBlocks (grain cloud, swarm), TimeFxBlocks (tape stop,
                      bouncing ball, pattern delay), TextureBlocks (chaos mod, acoustic sim), PlayedBlocks (strum
                      machine, hard tune, drum trigger), InstrumentBlocks (fart, trumpet, violin, piano); MidiOutBlock
                      (MIDI notes from your playing, mono with bend or poly))
  blocks/TrackedSynth.hpp  the mono synths' shared base (note tracking, glide, octave, amplitude)
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
                      BlockImages.cpp (generated) holds the built-in block images (none at the moment); BlockGlyphs.cpp the
                      symbol on each pedal's face; FileLooks.cpp each NAM/IR file's own amp/cab look, from its hash; ParamHelp.cpp the hover help texts; TextEdit the text fields' cursor,
                      selection and word editing (no DPF, tested)
tests/                doctest unit tests; TestSupport.hpp has a test block and an allocation counter; GuitarSignals.hpp a
                      synthetic guitar with ground truth for TrackingTests.cpp (the note tracking benchmark:
                      NAMSAYIN_TRACKING_BENCH=1 ./build/namsayin-tests -tc="*tracking benchmark"; PolyTrackingTests.cpp
                      is the chords' benchmark)
docs/                 Requirements and architecture
resources/fonts/      Jost (variable TTF + OFL.txt); tools/embed_font.py makes the Light and Regular instances into src/ui/FontData.cpp
resources/background/ The window background image (the owner's, generated with Krea-2 in ComfyUI); tools/embed_background.py makes it src/ui/BackgroundImage.cpp (JPEG)
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
- Pitch-following blocks use `TrackedInput` (Track as their last parameter, the `"lookahead"` config, `BlockDescriptor::lookahead` for the UI's Lookahead button). Lookahead stays off by default (owner, 2026-10-23). When touching the note tracking, run the benchmark before and after.
- Blocks send MIDI through `Block::setMidiOutput()` (`MidiOutput`: frames are the block's buffer's; the engine handles the timing and ends a block's notes when it's bypassed, removed or rebuilt). The plugin has a MIDI output (`DISTRHO_PLUGIN_WANT_MIDI_OUTPUT`); in Reaper the next plugin in the FX chain receives it.
- Blocks get the host tempo through `Block::setTempo()` and can show a level in the UI through `Block::meter()` (one hidden output parameter per slot, `slotMeterParam()`; the descriptor's `meterName` makes the panel show it). `Block::setRigInput()` gives blocks the chain's input (the noise gate's "Rig In" key).
- Tempo sync is generic: a descriptor's `syncParam`/`syncedParam`/`syncedIsRate` make the UI show note values (`TempoSync.hpp`); the block maps the knob itself. While the host plays, `Block::setSongPosition()` gives the position in quarter notes; synced LFOs lock to it (`Lfo::syncTo`), so they land on the beat.
- A `Choice` parameter's list must never grow or shrink once released: its normalized values would map to other choices in saved projects. Add a new block type instead.
- Every control needs a hover help text in `src/ui/ParamHelp.cpp` (plain English: what it does and how to use it), and so does every block type (`blockTypeHelp()`: what it is, what it's for, where it goes in the chain; shown when hovering a tile or a block in Add Block); `tests/ParamHelpTests.cpp` fails for a block type, block control or global control without one. Help is keyed by typeId and control name, so renaming a control means renaming its help entry.
- `tests/BlockSanityTests.cpp` runs every registered block at default, extreme and random settings (finite, bounded, no allocation, silent for silence). New blocks get behaviour tests too (`tests/PedalTests.cpp`, `tests/MorePedalTests.cpp` for the second set, `tests/SynthTests.cpp` for the synths, `tests/ThirdSetTests.cpp` for the third set). Every pedal also needs its own face symbol in `src/ui/BlockGlyphs.cpp` (`tests/BlockGlyphTests.cpp`).
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
- Metronome: see `docs/Architecture.md` 4.10. All its settings are host parameters (most hidden), not state, so they reach the DSP in every host. All its sounds are generated in code (`Metronome.cpp`), no samples (owner's decision): Classic recreates Fretbored's click exactly (a 1500/1000 Hz tone with 2nd and 3rd harmonics, 50 ms decay; fitted from the owner's WAVs), Digital and Beep are synthesized too. Don't add sample files.
- Block images: a block's sidecar image wins; otherwise its type's built-in image from `resources/blocks/<typeId>.*` (any format Pillow reads; embedded as 8-bit PNG, max 480 px high); otherwise the drawn placeholder art (the Neon style; owner's pick from six mockups; the Panel style was removed at the owner's request). After changing `resources/blocks/`, run `python3 tools/embed_block_images.py` (needs Pillow); never edit `BlockImages.cpp` by hand. There are none now: the Doubler's photo was removed so every block has the neon look (owner, 2026-10-23). Each pedal's face symbol is in `src/ui/BlockGlyphs.cpp` (one entry per typeId); a new block needs one, and `tests/BlockGlyphTests.cpp` fails without it or if it matches another pedal's. NAM and IR blocks instead get a look per file from the content hash (`src/ui/FileLooks.*`, owner 2026-10-24); its mapping is pinned by `tests/FileLookTests.cpp`, since changing it changes how everyone's files look.
- Tuner: see `docs/Architecture.md` 4.9. The plugin runs `Engine::analyzeTuner()` on its own thread every 20 ms while the tuner is on. Accuracy tests are in `tests/TunerTests.cpp`; keep them passing (±0.1 cent from 31 to 1319 Hz) when touching the detector.
- Split/merge: see `docs/Architecture.md` 4.4. The merge controls are host parameters (after the slots). Parallel processing is the Settings toggle; the DSP reads it when it builds a chain.
- Not done yet in the NAM block: resampling when the host rate differs from the model's training rate.
- IR block: WAV files via NAM Core's `nam::detail::load_wav_ir` (PCM 16/24/32, float 32; channels stored contiguously), linearly resampled to the host rate, energy-normalized, convolved with FFTConvolver's `TwoStageFFTConvolver` (head 64, tail 2048; zero latency). Low/High Cut are off at the ends of their ranges.
- NAM A2 Full/Lite: `nam::SlimmableModel::SetSlimmableSize(1.0)` = Full, `0.0` = Lite (`NAM/slimmable.h`). Not real-time safe — call it off the audio thread.
- Library paths: a file inside a zip is `archive.zip!/inner/path`. Blocks accept these directly: `materializeLibraryFile()` extracts to the cache dir on the non-RT thread. Block configs store `path` and `hash` (XXH3-128).
- Font: the UI draws in Jost, embedded as `src/ui/FontData.cpp`: Light (also the logo, large; the owner tried Bold and preferred Light), and Regular for small text (layout sizes under 13). Jost runs small, so the UI's `fontSize()` (which hides NanoVG's) draws text 1.69x the layout size (owner: too small at 1x, then asked for 30% more than 1.3x). Long names are shortened to fit with `fitText()`. Glyphs Jost lacks (★, ▾) fall back to DPF's DejaVu. After changing `resources/fonts/` or the weight, run `python3 tools/embed_font.py` (needs fontTools); never edit `FontData.cpp` by hand.
- Background: the window background (behind every panel, which are all black-filled, so it shows only between them) is `resources/background/background.*`, scaled to cover the window. After changing it, run `python3 tools/embed_background.py` (needs Pillow); never edit `BackgroundImage.cpp` by hand.
- Sidecar images must be 8-bit PNG or JPEG: DPF's bundled stb_image can't decode 16-bit PNG.
- Settings live in `~/.config/namsayin/settings.json` (Windows: `%APPDATA%\NAMsayin`); the library index caches are in `~/.cache/namsayin` (`%LOCALAPPDATA%\NAMsayin\Cache`). Every UI polls the settings file once a second, so instances stay in sync. Change settings with `updateSettings()` in the UI, which reloads before saving.
- Text fields (search, folders) need keyboard focus: focusing one calls `getWindow().focus()` to take it from the host. VST3 hosts may forward keys themselves (`onKeyDown`); DPF then sends character input with an empty `string`, so `onCharacterInput` encodes `character` itself. Some hosts keep keys for themselves; in Reaper, check that typing in the plugin window works (FX window option "Send all keyboard input to plugin" if not).
- MIDI learn: the map (CC → host parameter) is part of the chain state, so it travels like the chain and is in undo and presets. The DSP applies mapped CCs at the start of each buffer and reports every CC (`kParamMidiLastCc/LastValue/CcCount`); the UI learns from those, and also writes mapped values to the host, because LV2 hosts mostly can't take parameter changes from the DSP (VST3 can: `requestParameterValueChange`). The host must route MIDI to the plugin's track.
- Undo covers the chain (blocks, block values, bypass, MIDI map), not the global knobs. Call `beginEdit()` before changing `fChain` in the UI. The UI mirrors host slot values into `fChain` (`syncChainValue`), so presets save automated values too.
- Presets: Save and Save As write to the preset folder (`presetsDir()`, `presets/` in the config dir; subfolders allowed) through an in-app dialog, without embedding files. Export writes a copy anywhere through the system dialog and embeds files when "Embed files when exporting" is on. Open File… opens a preset from anywhere; Save on one outside the preset folder acts as Save As. The preset menu lists the preset folder's presets as they are on disk (folders as indented headings; no Recent list). Manage Presets… loads saved presets and sets the default (`Settings::defaultPreset`). New instances load the default in the UI (presets set host parameters): a second after the UI knows its instance ID, if the host gave it no saved chain (an empty chain state means a new instance). So it only happens once the UI has opened.
- Presets store the global sound controls (input mode, input, bass, middle, treble, output, limiter) and the merge controls by parameter symbol; ones a preset lacks go back to their defaults on load. Embedded files live under `files/<hash>/` in the zip and are extracted to `cache/presets/<hash>/` with their sidecar images.
- jalv 1.6.8 (the headless test host) crashes ("stack smashing detected", in its JACK thread) once the UI's chain state message passes about 1.4 KB (7-8 blocks): it copies UI messages into a fixed stack buffer. Not our bug; for longer chains in jalv use the `-DNAMSAYIN_TEST_NO_STATE_MESSAGE` build. That build found the UI reloading the exchange chain (and clearing undo) on every repeated instance-ID report; it now reloads only when the instance ID changes.
- DPF's file browser doesn't open in the headless test setup (no desktop portal), so presets were tested there through the Recent list; check Open/Save As in Reaper.
- Fretbored (the owner's other app, https://github.com/singularity098/fretbored) is the reference for the metronome and tuner features (`js/metronome.js`, `js/pitch.js`, `js/tuner.js`).
- Redraw rate: on Linux the UI can only redraw once per host idle tick (pugl processes X11 events and exposes from the host's idle/timer callback; DPF asks VST3 hosts for 16 ms, LV2 hosts choose their own rate, jalv ~30 Hz). A frame costs well under 1 ms of CPU, so drawing isn't the bottleneck. Windows redraws from the native message loop. The owner saw choppy dragging in Reaper on Linux in both LV2 and VST3. DPF leaves the GL swap interval at the driver default (usually vsync on), so each swap could block the host's GUI thread until the next refresh; the UI now turns vsync off for its window (`src/ui/Vsync.cpp`, GLX). Start the host with `NAMSAYIN_SHOW_FPS=1` to show a readout (idle ticks/s, frames/s, worst gap, draw time) to see what limits it. The owner's readout in Reaper: idle 36/s, frames 36/s, worst gap 31 ms, draw 0.17 ms, so Reaper's tick rate is the limit. Fix for VST3: `patches/dpf/0001-vst3-x11-event-handler.patch` registers the X11 connection with the host run loop's event handler, so mouse events are handled and redrawn as they arrive (owner: 70-80 frames/s in Reaper with it, on a 120 Hz monitor; was 36; the timer callback also handles events Xlib queued during its redraws, which changed nothing measurable). With a 1000 Hz mouse (readout: mouse 800-900/s) frames stay ~80/s and the worst gap ~21 ms while draws take 0.17 ms, so Reaper seems to dispatch run-loop fd events at only ~80 Hz; going further would mean not drawing on Reaper's thread at all (a render thread in DPF), which is a large, risky change. The readout also shows mouse motion events/s. LV2 has no such mechanism (only the host's idle calls), so in Reaper the VST3 version is the smooth one.
