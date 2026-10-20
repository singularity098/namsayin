# NAMsayin v1 — Architecture and Milestones

This plan covers version 1 as scoped in [Requirements.md](Requirements.md): local NAM blocks, a delay block and a reverb block, plus everything around them (chain, split/merge, tuner, metronome, presets, settings). Later features (Tone3000, IR loader, other effect categories) are not built in v1, but the design leaves room for them.

## 1. Tech stack

| Area | Choice | License |
| --- | --- | --- |
| Plugin framework | [DPF](https://github.com/DISTRHO/DPF) — VST3 and LV2 | ISC |
| NAM | [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) (brings Eigen and nlohmann/json) | MIT / MPL2 / MIT |
| UI | DPF's DGL with NanoVG | ISC / zlib |
| Zip reading | miniz | MIT |
| Content hash | xxHash (XXH3-128) | BSD-2 |
| Tests | doctest | MIT |
| Font | Inter | OFL |
| Build | CMake (DPF's `dpf_add_plugin`), dependencies as git submodules under `deps/` | — |

Targets: Linux and Windows, x86-64 with AVX2/FMA required (`-mavx2 -mfma` / `/arch:AVX2`). VST3 and LV2 only; no standalone app for now. Compilers: GCC/Clang on Linux, MSVC on Windows.

## 2. Repository layout

```
CMakeLists.txt
deps/                 DPF, NeuralAmpModelerCore, miniz, xxHash, doctest (submodules)
src/
  plugin/             DPF Plugin subclass: parameters, state, run()
  engine/             Real-time audio engine (no allocation, no locks)
    Engine            Owns the active ChainGraph; global input/output stages
    ChainGraph        Immutable processing graph built from a ChainState
    Block             Base class for every block
    BlockRegistry     Table of available block types (the "modular effects" hook)
    blocks/           NamBlock, DelayBlock, ReverbBlock
    global/           InputStage, ToneStack (Bass/Mid/Treble), Limiter, Meters
    Tuner, Metronome
    ParallelWorker    Second-core thread for split branch B
  state/              Plain data, shared by DSP and UI
    ChainState        Blocks, slots, split/merge, per-block settings (JSON)
    Preset            Load/save, embedding, missing-file handling
    Settings          Global per-user config, favorites
    Library           NAM/IR scanner, zip support, hashing, sidecar images
    UndoStack
  ui/                 DPF UI subclass and NanoVG widgets
    widgets/          Knob, Toggle, Button, Meter, BlockTile, ChainView, Dialog, ...
    theme/            Colours, sizes, fonts
resources/            Fonts, generic block images, metronome sounds
tests/
docs/
```

## 3. Signal flow

```
in ─► Input level ─┬─► Chain (up to 32 blocks, optional split A/B → merge) ─► Bass/Mid/Treble ─► Output level ─► (+ metronome) ─► Limiter ─► meters ─► out
                   │
                   └─► boost ─► Tuner analysis (dry input)
```

- **Global bypass** passes the input straight to the output (meters keep running).
- **Tuner mute** silences the guitar path only; the metronome stays audible.
- **Metronome** is mixed in before the limiter, so clicks cannot clip the output.
- **Mono mode** processes one channel and outputs it to both sides. **Stereo mode** runs every block on two channels; a NAM block then holds two model instances (double CPU, as agreed).

## 4. Engine design

### 4.1 Blocks are modular

Every effect is a subclass of `Block`:

```cpp
struct BlockDescriptor {
    const char* typeId;         // "nam", "delay", "reverb" — stored in presets
    const char* displayName;
    Category category;          // Local NAM, Delay, Reverb, ... (Add Block dialog)
    ParamSpec params[kParamsPerSlot];
    Block* (*create)();
};

class Block {
public:
    virtual void prepare(double sampleRate, uint32_t maxFrames, int channels) = 0;
    virtual void process(float** io, uint32_t frames) = 0;   // real-time safe
    virtual void setParam(int index, float value) = 0;
    virtual uint32_t latency() const { return 0; }
    virtual void reset() = 0;
};
```

New effects are added by writing one class and one registry line. Removing an effect is deleting both. The Add Block dialog, parameter names and preset loader all read from the registry.

### 4.2 Real-time safety

The audio thread never allocates, locks, or touches files.

1. Edits (add/move/remove block, change model, split/merge) update the `ChainState` on a non-audio thread.
2. A worker thread builds a new `ChainGraph`. NAM files are loaded and warmed up there, and blocks that didn't change are reused, so their state (delay tails, reverb tails) carries over.
3. The finished graph is handed to the audio thread through a lock-free single-slot exchange. The audio thread picks it up at the start of a buffer and crossfades for ~10 ms to avoid clicks.
4. The old graph goes back through a lock-free queue and is deleted off the audio thread.

Parameter changes (knobs, automation, MIDI) don't rebuild anything. They go straight to the block through its slot, with per-parameter smoothing.

### 4.3 Parameter slots (DAW automation and MIDI)

The host sees a fixed parameter list:

- **Global**: input mode, input, bass, middle, treble, output, global bypass, limiter on/off, tuner on/off, tuner mute, metronome on/off, metronome volume, and a few more.
- **32 slots × 9 parameters**: slot bypass plus 8 block parameters. That's ~290 parameters in total, which hosts handle fine.

Each block is assigned a free slot when it is created and keeps it for its whole life, so dragging a block to a new position doesn't break automation. Host-facing names are generic ("Slot 05 · P2"). The UI shows real names, and the slot number on each block tile so users can find it in their DAW.

**MIDI learn**: right-click any control → "MIDI Learn", then move a controller. The mapping (CC → parameter) is stored in the plugin state, and incoming CCs are applied in `run()`. This works the same in VST3 and LV2.

### 4.4 Split, merge and the second core

- The chain has at most one split. `ChainState` stores it as `{splitBefore: i, mergeAfter: j, branchA: [...], branchB: [...]}`.
- The merge has A/B balance, level, pan and phase invert per branch.
- If the two branches have different latencies, the shorter branch is delayed to match.
- **Parallel processing** (Settings toggle, on by default): a `ParallelWorker` thread is woken each buffer to run branch B while the audio thread runs branch A, and the audio thread then waits for it. The worker uses real-time priority where the OS allows it. If the worker hasn't finished in time, the audio thread waits briefly and then does nothing extra (no extra latency, but a risk of a dropout on an overloaded machine). With the toggle off, both branches run in sequence on the audio thread.

### 4.5 Latency

- All v1 blocks are zero latency. The engine still adds up block latencies along the path, aligns branches, and reports the total to the host (DPF `setLatency`), so later blocks (e.g. pitch, FFT convolution) just work.
- **Sample rate**: NAM models record the rate they were trained at, usually 48 kHz. If the host runs at that rate, there's no resampling. Otherwise the NAM block resamples with a low-latency resampler (a few samples), reports that latency, and shows a small warning badge on the block.

### 4.6 The NAM block

- Loads A1 and A2 files through NeuralAmpModelerCore. **To verify at M2**: the minimum Core version with A2 support, and how Full/Lite is selected through its API.
- A2 size: global default from Settings, with a per-block override.
- Normalization (per-block toggle): uses the loudness value stored in the `.nam` metadata to bring models to a common level. Models with no loudness value are left as they are, with a badge saying so.
- Block parameters: input gain, output gain, normalization on/off, A2 size (Full/Lite).

### 4.7 Delay block

Parameters: time (ms, or a note value when synced), tempo sync on/off, feedback, mix, tone (low-pass in the feedback path), mode (mono / stereo / ping-pong), stereo spread, modulation depth. In stereo mode, left and right have separate lines, offset by the spread setting.

### 4.8 Reverb block

An algorithmic reverb written in-house (Dattorro-style plate/hall network). Parameters: type (room / hall / plate), decay, pre-delay, damping, low cut, high cut, width, mix. Mono input gives a stereo-wide tail when the plugin is in stereo mode.

### 4.9 Tuner

- The dry input (plus boost) is copied into a lock-free ring buffer, and the analysis runs on a DSP-side worker thread, not the audio thread.
- Pitch detection: McLeod Pitch Method (as in Fretbored), with parabolic peak interpolation and an analysis window long enough for 31 Hz (~100 ms).
- The detected frequency, confidence and cents are sent to the UI as output parameters, so this works even when an LV2 host runs the UI in a separate process. Traditional and strobe views are both drawn in the UI from these values.
- Reference pitch A4 is adjustable from 415 to 466 Hz.
- **Accuracy target: ±0.1 cent from 31 Hz up to the 24th fret of the high E string (~1319 Hz)**, checked by automated tests with synthetic and recorded signals.

### 4.10 Metronome

- Runs in the DSP, scheduled to the exact sample.
- **Free mode** (default): its own tempo and start/stop.
- **DAW sync** (switch): follows the host's tempo, time signature and transport position (DPF `TimePosition`).
- Features match Fretbored: 20–300 BPM, tap tempo, meter and beat grouping, per-beat accents, subdivisions, Classic/Digital/Beep sounds with separate subdivision volume, step and gradual trainer, gap trainer, visual pulse.
- Sounds: the Classic and Digital samples from Fretbored, built into the binary. Beep is generated in code.

## 5. Plugin state, presets and settings

- **Plugin state** (saved by the DAW with the project): a JSON string through DPF's state API, holding the chain, slots, split, block settings, MIDI mappings and metronome settings. File references store the path and the content hash.
- **Preset files** (`.namsayin`): a zip containing `preset.json` and, if "embed files" is ticked when saving, the NAM files and images it uses. The top menu has New, Open, Save, Save As and a recent-presets list.
- **Missing files**: the block is created grayed out (passes audio through) with a "Locate…" button. If a library file with the same content hash exists, it is offered first.
- **Settings and favorites**: one JSON file per user — `~/.config/namsayin/settings.json` on Linux, `%APPDATA%\NAMsayin\settings.json` on Windows. It's written atomically, and every open instance reloads it when it changes.
- **Favorites** are stored by content hash (XXH3-128 of the file bytes).
- **Undo/redo**: a stack of `ChainState` snapshots (small, so snapshots are cheap). Consecutive moves of one knob are merged into one step. Ctrl+Z / Ctrl+Shift+Z while the window has focus.

## 6. Library

- Scans the NAM directory (and later the IR directory) on a background thread, including inside `.zip` files. Files inside zips are addressed as `archive.zip!/folder/model.nam`.
- An index cache (path, size, modification time, hash, metadata) keeps rescans fast. Only new or changed files are hashed.
- **Sidecar images**: for `My Amp.nam`, it looks for `My Amp.png` and then `My Amp.jpg` next to it (inside zips too). Otherwise it uses the generic image.

## 7. User interface

- DPF UI with NanoVG drawing. Resizable, with HiDPI scaling. The default logical size is ~1200 × 720 and the minimum is ~900 × 560.
- Layout:
  - **Top bar**: preset menu and preset name, undo/redo, global bypass; on the right, the Fretbored link, metronome and tuner buttons.
  - **Middle**: the chain, left to right, with square block tiles and "+" boxes. A split shows as two rows that rejoin at the merge.
  - **Below the chain**: the selected block's controls.
  - **Bottom bar**: mono/stereo toggle, input, bass, middle, treble and output knobs, input/output meters with clip lights (click to reset), limiter on/off.
- **Interactions**:
  - Drag a block to move it; drop indicators show where it lands.
  - Right-click a block: bypass, remove, MIDI learn.
  - Double-click a knob to reset it; Shift-drag for fine control.
- **Dialogs**: Add Block (categories on the left), Settings, and the Tuner and Metronome panels (overlays).
- A dark theme defined as colour tokens in one file, so it can be restyled.
- The Fretbored link opens the browser through a small platform helper (`xdg-open` / `ShellExecute`).

## 8. Build, test and release

- **CI** (GitHub Actions):
  - Linux and Windows builds of VST3 and LV2 on every push
  - unit tests
  - plugin validation with pluginval (VST3) and lv2lint (LV2)
- **Unit tests**:
  - tuner accuracy
  - metronome sample timing
  - delay and reverb behaviour
  - chain build and state round-trips
  - preset embedding and missing files
  - zip scanning
  - undo/redo
- **Real-time check**: a test build that fails if the audio thread allocates memory.
- **Manual DAW testing**: Reaper first, on Linux and Windows (it's the DAW the project owner uses); Ardour and Carla on Linux as secondary checks.
- **Releases**: zip files per platform attached to GitHub Releases.

## 9. Performance targets

- An empty chain uses under 0.5% of one core at 48 kHz with a 64-sample buffer.
- Engine overhead around blocks stays under 5% of the blocks' own cost.
- Changing a model or editing the chain causes no dropouts or clicks.
- The UI opens in under 300 ms.
- A library rescan of 1,000 files with an existing index takes under 1 s.

## 10. Milestones

Each milestone ends with a working build, passing tests and a short demo.

| # | Milestone | Done when |
| --- | --- | --- |
| M0 | **Project skeleton** | CMake + submodules; a pass-through plugin builds as VST3 and LV2 on Linux and Windows in CI; an empty resizable HiDPI NanoVG window; MIT license headers |
| M1 | **Engine core** | Block interface and registry; ChainGraph with real-time-safe swap; parameter slots; state save/restore; input/tone stack/output/limiter/meters; global bypass; mono/stereo |
| M2 | **NAM block** | A1 and A2 loading on a background thread; Full/Lite global and per block; normalization; sample-rate handling with latency reporting |
| M3 | **Delay and reverb** | Both blocks with their parameters, stereo delay and tempo sync |
| M4 | **Main UI** | Chain view with tiles, drag-and-drop, "+" and the Add Block dialog, block controls, bottom bar with knobs and meters, generic images, dark theme |
| M5 | **Library and settings** | Settings panel; NAM directory scanning with zip support, rescan button, index cache; hashing and favorites; sidecar images |
| M6 | **Split and merge** | One split with merge controls, latency alignment, branch B on a second core |
| M7 | **Presets, undo/redo, MIDI learn** | Preset menu, embedding, missing-file handling, undo/redo, MIDI learn |
| M8 | **Tuner** | Traditional and strobe views, mute, reference pitch, accuracy target met in tests |
| M9 | **Metronome** | Full Fretbored feature set, free and DAW-sync modes |
| M10 | **v1.0 release** | Performance targets met, DAW testing done, docs, first GitHub Release |

M2–M3 and M4 can overlap, as can M8 and M9.

## 11. Decisions log

1. **CPU baseline**: AVX2/FMA is required.
2. **Standalone app**: dropped for now; VST3 and LV2 only.
3. **Reverb types**: room, hall and plate.
4. **Test DAW**: Reaper is the primary host.
