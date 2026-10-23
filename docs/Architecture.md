# NAMsayin v1 — Architecture and Milestones

This plan covers version 1 as scoped in [Requirements.md](Requirements.md): local NAM blocks, local IR blocks, a delay block, a reverb block and a compressor block, plus everything around them (chain, split/merge, tuner, metronome, presets, settings). Later features (IR loader, other effect categories) are not built in v1, but the design leaves room for them.

## 1. Tech stack

| Area | Choice | License |
| --- | --- | --- |
| Plugin framework | [DPF](https://github.com/DISTRHO/DPF) — VST3 and LV2 | ISC |
| NAM | [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) (brings Eigen and nlohmann/json) | MIT / MPL2 / MIT |
| UI | DPF's DGL with NanoVG | ISC / zlib |
| Zip reading | miniz 3.1.0 | MIT |
| Content hash | xxHash 0.8.3 (XXH3-128) | BSD-2 |
| Tests | doctest | MIT |
| Font | Jost Light and Regular (embedded; `resources/fonts`, `tools/embed_font.py`) | OFL |
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
    blocks/           NamBlock, IrBlock, DelayBlock, DoppelgangerBlock, ReverbBlock, CompressorBlock
    global/           InputStage, ToneStack (Bass/Mid/Treble), Limiter, Meters
    tuner/            PitchDetector, Tuner (done, M8)
    metronome/        Metronome (done, M9; its sounds are generated in code)
    ParallelWorker    Second-core thread for split branch B (done, M6)
  state/              Plain data, shared by DSP and UI
    ChainState        Blocks, slots, split/merge, per-block settings (JSON)
    Preset            Load/save, embedding, missing-file handling
    Settings          Global per-user config, favorites
    Library           NAM/IR scanner, zip support, hashing, sidecar images
    UndoStack
  ui/                 DPF UI subclass and NanoVG widgets
    widgets/          Knob, Toggle, Button, Meter, BlockTile, ChainView, Dialog, ...
    theme/            Colours, sizes, fonts
resources/            Fonts, generic block images, background
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
    Category category;          // NAM, IR, Delay, Reverb, ... (Add Block dialog)
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
    virtual void setTempo(double bpm) {}            // host tempo, before every process()
    virtual float meter() const { return 0; }       // a level the UI shows, e.g. gain reduction
};
```

A block's `meter()` reaches the UI through one output parameter per slot (`kParamSlotMeterBase + slot`).

New effects are added by writing one class and one registry line. Removing an effect is deleting both. The Add Block dialog, parameter names and preset loader all read from the registry.

### 4.2 Real-time safety

The audio thread never allocates, locks, or touches files.

1. Edits (add/move/remove block, change model, split/merge) update the `ChainState` in the UI, which sends it to the DSP as the `chain` plugin state.
2. The DSP's `setState()` (always called off the audio thread) builds a new graph. NAM files are loaded and warmed up there, and blocks that didn't change are reused, so their state (delay tails, reverb tails) carries over.
3. The finished graph is handed to the audio thread through a lock-free single-slot exchange. The audio thread picks it up at the start of a buffer, fades the chain output out over 5 ms, swaps, and fades back in over 5 ms. (A fade out/in rather than a crossfade, because reused blocks can't run in two graphs at once.)
4. The old graph goes back through a lock-free queue and is deleted off the audio thread.

Parameter changes (knobs, automation, MIDI) don't rebuild anything. They go straight to the block through its slot, with per-parameter smoothing.

Parameter values live in the host. `ChainState` also stores each block's normalized values and bypass, but only for presets and undo: when one is loaded, the UI pushes those values to the host. The DSP always reads the host's values.

### 4.3 Parameter slots (DAW automation and MIDI)

The host sees a fixed parameter list:

- **Global**: input mode, input, bass, middle, treble, output, global bypass, limiter on/off, tuner on/off, tuner mute, metronome on/off, metronome volume, and a few more.
- **32 slots × 17 parameters**: slot bypass plus 16 block parameters (8 until the Doppelganger Delay needed more; changed before release, so no saved automation was affected). That's ~550 slot parameters, which hosts handle fine.

Each block is assigned a free slot when it is created and keeps it for its whole life, so dragging a block to a new position doesn't break automation. Host-facing names are generic ("Slot 05 · P2"). The UI shows real names, and the slot number on each block tile so users can find it in their DAW.

**MIDI learn**: right-click any control (or a block tile, for its bypass) → "MIDI Learn", then move a controller. The mapping (CC + channel → host parameter) is stored in the chain state, so it is saved with the project, in presets and in undo, and reaches the DSP by the same routes as the chain. Incoming CCs are applied at the start of each buffer in `run()`. The DSP reports each CC through hidden output parameters: the UI learns from them, and writes mapped values to the host, since LV2 hosts mostly can't take parameter changes from the DSP (in VST3 the DSP also reports them itself). Right-click → "Clear MIDI" removes a mapping; removing a block removes its mappings.

### 4.4 Split, merge and the second core

- The chain has at most one split. `ChainState` keeps the blocks in one list, left to right, plus `split = {begin, end}`: blocks `[begin, end)` run in the two branches, and each of them has a `branch` tag (A or B). Blocks before `begin` run before the split, blocks from `end` on after the merge. A branch may be empty; it then carries the signal unchanged (a parallel dry path). `ChainState` has helpers (`sectionIndices`, `insert`, `remove`, `move`, `addSplit`, `removeSplit`) that keep the range and tags consistent. Removing the split keeps every block: the branches become serial, A then B. The chain JSON went to version 2 for this; version 1 still loads.
- The merge has A/B balance, and a level, pan and phase invert per branch. These are host parameters (`kParamMergeBalance` … `kParamMergeInvertB`), so they can be automated; one set is enough, as there's only one split. Presets save them. Balance keeps both branches at full level in the middle and turns the other one down towards either end. Pan works like a balance control in Stereo mode and is ignored in Mono mode (the chain then has one channel). All merge gains are smoothed, so phase invert fades through zero instead of clicking.
- If the two branches have different latencies, the shorter branch is delayed to match (`DelayLine`), and the host is told the longer one.
- **Parallel processing** (on by default; Settings > Parallel processing. The DSP reads the setting when it builds a chain, and the UI resends the chain when it changes): a `ParallelWorker` thread is woken each buffer to run branch B while the audio thread runs branch A, and the audio thread then waits for it. Waking is lock-free (an atomic counter with `wait`/`notify_one`), and the audio thread spins briefly, then yields, until the worker is done. The worker copies the audio thread's scheduling policy and priority on its first job (Linux), or uses `THREAD_PRIORITY_TIME_CRITICAL` (Windows), and flushes denormals like a host audio thread. If the worker is late, the audio thread keeps waiting (no extra latency, but a risk of a dropout on an overloaded machine). With the toggle off, or with an empty branch B, both branches run in sequence on the audio thread. The worker thread is only started once a chain needs it.
- UI: with a split, the chain view has two rows of slightly smaller tiles: branch A and everything outside the split on the top row, branch B below, and a tall merge tile where they join (click it for the merge controls, × to remove the split). Each branch and the part after the merge has its own "+".  While there's no split, hovering the gap before a block (or before the "+") shows a small split icon on the signal line; clicking it adds an empty split there (both branches start empty). Blocks are dragged between rows and sections freely; a line in the highlight colour shows where a block will land.

### 4.5 Latency

- All v1 blocks are zero latency. The engine still adds up block latencies along the path, aligns branches, and reports the total to the host (DPF `setLatency`), so later blocks (e.g. pitch, FFT convolution) just work.
- **Sample rate**: NAM models record the rate they were trained at, usually 48 kHz. If the host runs at that rate, there's no resampling. Otherwise the NAM block resamples with a low-latency resampler (a few samples), reports that latency, and shows a small warning badge on the block.

### 4.6 The NAM block

- Loads A1 and A2 files through NeuralAmpModelerCore (pinned to v0.6.0, which includes the A2 fast path). Full/Lite is selected with `nam::SlimmableModel::SetSlimmableSize` (1.0 = Full, 0.0 = Lite); it isn't real-time safe, so it's applied off the audio thread.
- A2 size: global default from Settings, with a per-block override.
- Normalization (per-block toggle): uses the loudness value stored in the `.nam` metadata to bring models to a common level. Models with no loudness value are left as they are, with a badge saying so.
- Block parameters: input gain, output gain, normalization on/off, A2 size (Full/Lite).

### 4.7 Delay block

Parameters: time (ms, or a note value when synced), tempo sync on/off, feedback, mix, tone (low-pass in the feedback path), mode (mono / stereo / ping-pong), stereo spread, modulation depth. In stereo mode, left and right have separate lines, offset by the spread setting.

- Time changes glide (a smoothed, fractionally read delay time), like a tape or analog delay, instead of clicking.
- Synced, the Time knob picks from 14 note values (1/1 down to 1/32, with dotted and triplet values); the host tempo comes from DPF's `TimePosition`, 120 BPM when the host doesn't report one. Max delay is 4 s.
- The feedback path has a fixed 40 Hz high-pass, the Tone low-pass and a soft limit above half scale, so 100% feedback stays bounded.
- Ping-pong: the input (summed to mono) enters on the left, repeats alternate left and right.
- Mix: the dry signal stays at full level up to 50%, and the wet signal reaches full level at 50% (also used by the reverb).

### 4.7b Doppelganger Delay block

The owner's idea: a delay whose echoes alternate between two effects. With the default order, echoes 1, 3, 5… are flanged and echoes 2, 4, 6… are chorused, and each echo has only its own effect (they don't stack).

- An ordinary feedback delay keeps every echo in one loop, so effects in the loop would pile up. Instead, one clean line repeats at 2T with feedback g²: `s(t) = x(t) + g² F² s(t−2T)`. Reading it at T gives the odd echoes, `g F s(t−T)`; the loop's own feedback term gives the even ones. These are exactly the echoes of a delay with time T and feedback g, split by parity, so each stream goes through its own effect outside the loop. F is the Tone low-pass (and a 40 Hz high-pass), once per repeat, so repeats darken like the Delay block's.
- Flanger: a 0.25–5.25 ms swept delay with regeneration (−95…95%), level-compensated. Chorus: 1–3 voices around 10 ms, spread evenly around the LFO cycle. Each has rate, depth and mix (0% = clean echoes).
- Order swaps which effect gets the odd echoes. Width offsets the right channel's LFOs (up to 180° for the flanger, 90° for the chorus). Bounce leans flanged echoes left and chorused echoes right (stereo mode).
- Time, Sync, Feedback and Mix work as in the Delay block (same note values; synced up to 4 s, so the line holds 8 s).
- 16 parameters: time, sync, feedback, mix, tone, order, width, bounce, then flange rate/depth/regen/mix and chorus rate/depth/voices/mix.

### 4.8 Reverb block

An algorithmic reverb written in-house (Dattorro-style plate/hall network). Parameters: type (room / hall / plate), decay, pre-delay, damping, low cut, high cut, width, mix. Mono input gives a stereo-wide tail when the plugin is in stereo mode.

- One figure-eight tank (Dattorro 1997) voiced three ways: Plate is the original sizes; Hall scales the tank by 1.45 with deeper, slower modulation; Room scales it to 0.42 with less diffusion and adds early reflections tapped from the pre-delay line.
- Decay is the RT60 in seconds; the tank's decay gain is computed from it and the tank's loop time, so the set time holds for every type and sample rate (tested).
- Buffers are sized for the largest voicing in `prepare()`, so changing type on the audio thread only changes read positions.

### 4.8b Compressor block

Added to v1 at the owner's request. Studio style, feed-forward: stereo-linked peak detector, soft-knee gain computer, attack/release smoothing of the gain in dB, then makeup gain and a dry/wet mix for parallel compression. Parameters: threshold, ratio, attack, release, knee, makeup, mix. Its gain reduction is shown in the block panel through the block meter.

### 4.9 Tuner

- The input (after the Input level, before the chain) is copied into a lock-free ring buffer, and the analysis runs on a DSP-side thread every 20 ms while the tuner is on, never on the audio thread (`engine/tuner/Tuner`). Stereo input is analysed as the sum of both channels.
- The detector doesn't depend on the level, so quiet inputs work as well as loud ones (this is what the "boost" in the signal flow stands for): only a gate at -74 dBFS RMS decides whether there's anything to analyse.
- Pitch detection (`engine/tuner/PitchDetector`): McLeod Pitch Method (as in Fretbored) on an ~85 ms window (4096 samples at 48 kHz, enough for 31 Hz), FFT autocorrelation and parabolic peak interpolation, which on its own is good to about ±0.4 cent. It is then refined: a Hann-weighted normalized cross-correlation over a whole number of periods, low-passed above the 6th harmonic, measured at the largest multiple of the period that fits and divided back. The refinement uses up to four windows of audio (~340 ms) when the note has been steady that long, which makes it far less sensitive to noise.
- Only audio at a steady level is analysed: levels are compared in chunks of whole periods, and a window that isn't steady (a note starting, stopping or changing) gives no reading at all, rather than a wrong one. The UI keeps showing the last note for a second.
- The detected frequency and clarity are output parameters, so this works when an LV2 host runs the UI in a separate process. The UI takes the median of the last five readings (starting over when the note clearly changes) and works out the note and cents from the reference pitch. Traditional (needle, ±50 cents, green within ±2 cents) and strobe (three rows of bands for the fundamental and two octaves, drifting at a speed proportional to the error) views are drawn in the UI; the view is a hidden parameter, so it's saved with the project.
- While the tuner is on, the guitar is muted by default (Tuner Mute; also in global bypass); the metronome stays audible.
- Reference pitch A4 is adjustable from 415 to 466 Hz.
- **Accuracy target: ±0.1 cent from 31 Hz up to the 24th fret of the high E string (~1319 Hz)**, checked by tests at 44.1, 48 and 96 kHz on every half semitone, with pure tones, harmonic-rich tones and noisy tones with a weak fundamental (worst case about 0.01 cent), and through the start and end of notes (a gated tone; a plucked-string-like note whose harmonics decay at different rates). No recorded signals yet: the tests are synthetic.

### 4.10 Metronome

- Runs in the DSP (`engine/metronome/Metronome`), scheduled to the exact sample: each beat's time is kept as a fractional sample position and rounded, so the tempo never drifts, whatever the buffer size. Clicks are mixed in after the Output level and before the limiter, so they can't clip the output; the tuner's mute doesn't silence them. Global bypass does (it's a true bypass).
- **Free mode** (default): its own tempo, starting on beat 1 when switched on. Switching off lets the click that's sounding play out (cutting it would pop).
- **DAW sync** (switch): follows the host's tempo, time signature and transport position (DPF `TimePosition`): clicks fall where the host's beat position crosses a beat or subdivision, only while the transport plays. Each click has an ID (beat × 16 + subdivision), so a click on a buffer boundary is never played twice or missed, and a jump (loop, locate) starts over at the new position. The tempo trainer doesn't apply in sync mode (the host sets the tempo); the gap trainer does, on the host's bars.
- Features match Fretbored: 20–300 BPM (slider, ±1/±5, keyboard), tap tempo, 1–16 beats over 2/4/8/16 with beat grouping (cycled through every split into 2s and 3s; the default follows Fretbored, e.g. 7/8 = 2+2+3), per-beat accents (accent/normal/soft/mute, set by the grouping and by clicking a beat), the eleven subdivisions, Classic/Digital/Beep sounds with separate volume for subdivisions, step and gradual tempo trainer (also slowing down), gap trainer, and a visual pulse (the pendulum sweep was removed at the owner's request, 2026-10-23: at host redraw rates it looked choppy). Fretbored's "visual sync" offset (for Bluetooth speakers) isn't needed in a plugin.
- **Every setting is a host parameter** (tempo, volume, on/off and sync are automatable; the rest are hidden), so it works the same in every host — including Reaper's LV2, which doesn't deliver UI state messages — and is saved with the project. The accents are 16 parameters; the grouping is a bit mask of the beats that start a group, kept only for the UI. The DSP reports the beat being heard, a beat counter (the UI pulses when it changes), the bar, the tempo being played and the gap trainer's mute as output parameters.
- **Panel**: the top bar's Metronome button switches it on and off (and flashes on every beat); ▾ next to it opens the panel, which covers the chain while it's open. Shortcuts while the panel is open (so only while the plugin window has the keyboard focus): Space start/stop, ↑/↓ tempo (Shift: 5 BPM), T tap.
- Sounds: all generated in code at the host rate, no samples (owner's decision, 2026-10-23, so every sound is our own). Classic recreates Fretbored's click, which turned out to be a formula: fitting the owner's two WAVs gave exactly e^(-t/50 ms) (0.5 sin wt + 0.15 sin 2wt + 0.05 sin 3wt), 50 ms long, at 1500 Hz (accent) and 1000 Hz (beat and subdivisions), with zero residual; with the same 0.5 ms / 6 ms edge fades the generated clicks match the old samples to -56 dB (16-bit rounding). Digital (a short band-limited square wave) and Beep were already generated in code.
- The top bar also has the Fretbored link (opens the web app in the default browser: `xdg-open` / `ShellExecute`).

### 4.11 Pedals

Added before v1 at the owner's request (Requirements, "Pedals"): every pedal is a block like the others, written from scratch, with several varieties as a `Type` choice where that makes sense. All are zero latency and real-time safe (buffers sized in `prepare()`), and each has at most 16 controls (most use 8 or fewer).

| Category | Blocks (types) |
| --- | --- |
| Distortion | Boost (Clean, Treble, Mid), Overdrive (Screamer, Blues, Transparent), Distortion (RAT, DS-1, Metal), Fuzz (Face, Muff, Octave), Gated Fuzz, Envelope Drive, Bitcrusher |
| Dynamics | Compressor, Noise Gate (Gate, Expander; key from the block or the rig input), Volume (Audio, Linear taper), Auto Swell, Kill Switch |
| EQ | Graphic EQ (7 bands, GE-7 centres), Parametric EQ |
| Modulation | Chorus (Classic, Dimension, Tri, Vibrato), Flanger, Phaser (4/6/8/12 stage, Vibe), Barber Pole, Modulation Sweeper, Tremolo (Sine, Triangle, Square, Opto, Harmonic, Pan), Rotary, Doubler, Ring Mod, Warble, Slicer |
| Reverb | Reverb (Room, Hall, Plate), Spring Reverb, Shimmer, Freeze, Bloom Reverb, Gated Reverb |
| Delay | Delay, Tape Echo (seven head combinations), Ducking Delay, Reverse Delay, Pitch Delay, Beat Repeat, Looper |
| Pitch | Octave (analog style), Poly Octave, Pitch Shifter, Whammy, Harmonizer, Detune, Capo, Sustainer |
| Synth | Arpeggiator, Organ, Synth, Super Saw, FM Synth, Fold Synth, Acid Synth, String Machine, Chip Synth, Poly Chip |
| Filter | Filter (LFO or random-step sweep), Robot Filter (sample and hold: random, 8-step loop or envelope) |
| Wah | Wah (expression), Auto Wah (Wah, LP, BP, HP voicings), Talk Box (formant vowels: pedal, envelope or LFO) |

The second set (Requirements, "Pedals") reuses the DSP above wherever it can:

- **Envelope Drive** and **Gated Fuzz** are drives on the shared oversampled base. Envelope Drive moves its gain with the playing level and makes up the level from the envelope (level / clip(level x gain)), so the dynamics stay and only the dirt changes. Gated Fuzz feeds its clipped output back through a band-pass (Stab sets the loop gain, which oscillates below about 70%, and the squeal's pitch); the loop only lives while the input envelope is above -80 dB, so it dies away when you stop.
- **Reverse Delay** is a pitch voice at -1x: each tap sweeps backwards through a Time-long slice, Hann-faded, the two taps half a sweep apart (no extra latency; the slice starts at the newest sample). **Pitch Delay** puts a pitch voice (plain 40 ms window) inside the feedback loop, so each pass is shifted again. **Beat Repeat** keeps a history of the last 2 s; on Repeat it plays the last Slice of it (the history stops while repeating, so the slice stays intact), with per-repeat edge fades, gate, decay and a speed change (Pitch, capped at +-36 semitones).
- **Bloom Reverb** and **Gated Reverb** use the FDN tank. Bloom ducks the tank's feed on each new pick (`dsp/OnsetDetector.hpp`: a fast envelope jumping to twice a slow one) and swells it back over Bloom; Gated keys a hold-and-release gate on the wet signal from the input.
- **Slicer** and **Kill Switch** are gain envelopes: the slicer's step position follows the song while the transport plays (step 1 on the bar); the kill switch's Kill and Stutter are momentary parameters (with Latch, a press toggles).
- **Barber Pole**: three phasers sweeping 60 Hz to 3.8 kHz (six octaves), a third of a cycle apart, each weighted by sin^2 of its phase (the weights sum to 1.5), so each fades in at the bottom and out at the top. **Warble**: a delay modulated by a sine/random blend (wow) and a 9 Hz flutter, with a low-pass and soft saturation.
- **Detune**, **Capo**, **Arpeggiator**, **Organ** and **Sustainer** are pitch blocks on the shared pitch-synchronous base. The arpeggiator steps a smoothed shift through the chord's notes (count from the song when synced) with a gate envelope; the organ adds sub, octave, twelfth and two-octave voices to the fundamental and re-swells on each pick; the sustainer is an automatic gain (up to +36 dB) that holds the level the note was picked at, then crossfades into a shifted harmonic over Bloom.
- **The synths** (`blocks/SynthBlocks.*`) share a base with the Synth block's recipe: the YIN tracker with glide and an octave, an amplitude that follows the guitar's envelope (with Attack and Release), and the pick detector; band-limited saws and pulses come from `dsp/Oscillator.hpp` (PolyBLEP). Super Saw: seven saws detuned by up to Detune (uneven spread), panned by their detune, through an SVF low-pass. FM Synth: a sine modulator (ratio choice, self-feedback) on a sine carrier, its index falling from Index to Sustain over Decay after each pick, capped so the sidebands stay under 0.4 fs. Fold Synth: a sine folded by sin(pi/2 (g x + offset)) at 4x (the drives' oversampler), g up to 10 with the playing, then a low-pass gate whose single control sets both gain and cutoff (Follow: the amplitude; Pluck: a decaying ping on each pick). Acid Synth: a saw or square, tanh-driven, through two SVF low-passes (24 dB, resonance on the second) with a decaying envelope on each pick, louder picks accenting it, and a tanh on the output. String Machine: saws at 0.5x, 1x and 2x through a three-tap ensemble (delays around 4 ms swept by 0.63 Hz and 5.7 Hz sines, taps a third of a cycle apart, the right channel a sixth further). Chip Synth: notes snapped to semitones, PolyBLEP pulses (12.5/25/50%) or a 16-step triangle, a retriggered arpeggio, a vibrato fading in from 150 ms, and Crush quantizing the volume to 16 steps and the samples to fewer bits. **Pick Filter** (every mono synth, the Synth block included; owner's request after the Poly Chip's, 2026-10-23; `dsp/PickFilter.hpp`): a new note from the tracker sounds only once its pitch has held within half a semitone for that long; until then the synth stays silent (a fresh note) or keeps playing the old one (a jump while a note sounds), and a pick's onset waits with it, so plucked envelopes start with the note. Bends and vibrato within a sounding note pass at once. 0-60 ms, default 0 (off: every estimate passes, as before); it came after the first release, so it's each synth's last parameter.
- **Poly Chip** (`blocks/PolyChipBlock.*`): the polyphonic chip synth. It does not track a pitch; it splits the input into one band per semitone, C2..E6 (53 bands, each two SVF band-passes in series at Q 8, widening to Q 5 over the octave below C3 so the low strings ring up faster; a note a semitone away is about 5 dB down). A band's level is the magnitude of its last stage's band-pass and low-pass outputs, which are equal and a quarter cycle apart at the band's note, so it doesn't ripple. Every ~0.7 ms: local peaks over -54 dBFS and within 18 dB of the loudest band are candidate notes; Focus drops a peak when a peak 12, 19, 24, 28, 31, 34 or 36 semitones below (its note's 2nd-8th harmonics) is louder than 1/8 (0%) to 8x (100%) of it (default 70%: 2.3x; a guitar's 2nd harmonic is often twice its note); the loudest Voices (1-6) remain, playing notes counted 3 dB louder so voices don't flicker. **Pick Filter** (owner's request: the pick attack squeaked): a new note sounds only once it has been found for that long (default 15 ms) without falling below 60% of its peak meanwhile (the ring of the pick in the faster, higher bands dies away; a real note doesn't). On plucked-string test signals (Karplus-Strong, pick-shaped start, pickup tilt, 2 ms click) 0 ms gave 19-34 stray notes in the first 100 ms per string, 15 ms none; notes then start 26-53 ms after the pick (low E slowest). Each sounding band plays a PolyBLEP pulse or the 16-step triangle, its amplitude following the band level (1 ms attack, Release). **Quantize** on: at its exact semitone; off (owner's request): at the band's measured pitch, from the turn rate of its quarter-cycle pair (atan2 of successive pairs, smoothed over 10 ms, kept within a semitone of the band). A note starting next to (within 2 semitones of) a fading voice takes over that voice's phase, level and pitch, so bends and slides move one voice from band to band. Arp Up/Down plays the sounding notes one at a time in pitch order, switching in 1 ms, and restarts when the chord stops. Crush as in the Chip Synth. No latency; about 2% of a core (3% with Quantize off). Quantize and Pick Filter came after the first release, so they're the last two parameters. Trade-off: a played octave or twelfth above another note can be taken for its harmonic (lower Focus).
- **Robot Filter**: an SVF whose cutoff jumps to a new held value on each clock tick (random, a fixed 8-step random loop, or the input level), with a glide on the octave position. **Talk Box**: three SVF band-passes on the first three formants, interpolated (on a log scale) along a vowel path.

**Modulation Sweeper** (the owner's idea, 2026-10-23): two or three of chorus, flanger, phaser and vibrato, picked by the user (Effect 3 can be None), that the block moves between. Each Sweep Rate step it moves on to the next effect in the pattern (Back & Forth: 1 2 3 2 1..., Cycle: 1 2 3 1...); Glide is the share of each step spent crossfading (raised-cosine, gains adding to 1 because the effects all carry the dry signal; never shorter than 15 ms, so 0% doesn't click). Sync makes each step a note value, counted from the song's start, so moves land on the beat. All four effects run all the time (a two-voice chorus and the vibrato share one input delay line; the flanger has its own feedback line; a 6-stage phaser), so a crossfade never starts from a cold line, and each effect's weight is smoothed (10 ms) so changing the effects or pattern doesn't click. Depth, Mix, Feedback and Width are shared; each effect has its own rate. 15 controls.

Shared DSP (`src/engine/dsp/`):

- **Oversampler**: the drives clip at 4x through polyphase IIR half-band filters (HIIR-style allpass pairs designed in code): flat to 0.49 fs, aliases more than 70 dB down, no added latency (IIR group delay only). Clipping curves are in `Waveshapers.hpp` (x / (1 + |x|^n)^(1/n), square roots only).
- **Svf**: TPT state variable filter, stable when the cutoff moves every sample (wahs, auto wah, filter, synth). `OnePole` and `EnvelopeFollower` live with it.
- **Lfo** (phase accumulator, any phase offset for stereo) and **RandomLfo** (smoothed random, for drift). `TempoSync.hpp` maps a synced knob to a note value: a time knob has the longest note at the top (1/1 down to 1/32); a rate knob has its own list from 4 bars and 2 bars down to 1/32, the fastest at the top. The descriptor's `syncParam`/`syncedParam`/`syncedIsRate` tell the UI.
- **PitchShifter**: delay-line voices, two Hann-crossfaded taps whose delays move at (1 - ratio) samples per sample and jump by the window at the end of a sweep. Polyphonic, no reported latency; the shifted sound trails by about half the window (the Window control). The splices are made phase-coherent: a **PeriodFinder** (autocorrelation on a 12 kHz copy, every 8 ms, lags 7-60 ms, parabolic peak) finds the signal's repetition period, which for chords is their approximate common period (two root periods for a power chord, four for a major triad); the tracked note is the fallback. The window becomes a whole, even number of that period, each tap takes the new window when it jumps, and each jump is steered (a few percent of the window per jump, so it never clicks) to land exactly half a window from the other tap. Shifted notes and chords then keep 95-100% of their energy at the shifted partials (tested; it was 0-20% with a plain window, which is the warble cheap pitch shifters have). Costs about 2-3% of a core per pitch block in stereo. Used by Pitch Shifter, Whammy, Poly Octave, Harmonizer and Shimmer (Shimmer keeps a plain window: its reverb tail has no period).
- **PitchTracker**: YIN (cumulative-mean normalized difference, absolute threshold) on a copy decimated to 12 kHz, every 4 ms over a 17 ms window, 60-1500 Hz, then refined at the full rate around the estimate. On plucked strings (Karplus-Strong, whose fundamental is often weaker than its harmonics) it finds the note within 4-24 ms and holds it to 0.1 cent (tested). It replaced a zero-crossing tracker that locked onto harmonics of low plucked notes. Used by the Synth (oscillator pitch), the Harmonizer (which note, so which interval), the Octave (its detector's low-pass follows the note, so harmonics can't upset the flip-flops) and, as a fallback to the PeriodFinder, the pitch-synchronous windows.
- **Fdn**: 8-line Householder feedback delay network with input diffusers, per-line damping and RT60 gains, two modulated lines; it can freeze (lossless loop, input shut). Used by Shimmer, Freeze, Bloom Reverb and Gated Reverb.
- **OnsetDetector**: a new pick attack (a fast envelope jumping to twice a slower one, over -50 dB, at most every 60 ms). Used by the Organ, the Sustainer, Bloom Reverb and the synths.

Notes:

- **Levels**: default settings stay within a few dB of bypass (drives a few dB louder, like a pedal set at unity-plus).
- **Beat-locked LFOs**: while the host's transport plays, the engine passes each block the song position (quarter notes, at the start of each chunk) through `Block::setSongPosition()`. Synced tremolo, flanger, phaser and filter lock their LFO phase to it (cycles counted from the song's start; the tremolo's loudest point lands on the beat), only correcting when off by more than 0.1% of a cycle. Stopped, they run free at the synced rate.
- **Noise gate key**: `Block::setRigInput()` gives every block the chain input (after the input level). With Key = Rig In, a gate after an amp opens and closes on the clean guitar.
- **Momentary parameters** (`ParamScale::Momentary`, the looper's Rec/Dub, Play/Stop, Clear): 1 while held; blocks act on the 0 → 1 edge, so a MIDI footswitch's press/release maps directly. The UI holds a quick click for 80 ms so the press can't fall inside one audio buffer with its release.
- **Looper**: a fixed buffer of 60 s at 48 kHz per channel (23 MB), allocated in `prepare()`. Its status (state, length, position) is encoded in the block meter (`LooperStatus`) and drawn in the panel (`MeterStyle::Looper`). The loop survives chain edits (the engine reuses the block) but not a sample-rate change.
- **Spring reverb**: two springs, each a loop of 60 stretched first-order allpasses (the dispersion that makes the chirp), the transit delay and damping; after Välimäki, Parker and Abel (2010), simplified.
- **Tests**: `tests/BlockSanityTests.cpp` runs every registered block at default, extreme and random settings, at 44.1/48/96 kHz, mono and stereo (finite, bounded, no allocation, silent for silence), and for ten seconds at its maximum to check that no feedback loop grows once the input stops. `tests/PedalTests.cpp` and `tests/DriveTests.cpp` check each pedal's behaviour (gains, notches, rates, intervals, aliasing, looper actions).
- Voicing is by measurement, not by ear; the owner tunes the sound.

## 5. Plugin state, presets and settings

- **Plugin state** (saved by the DAW with the project): a JSON string through DPF's state API, holding the chain, slots, split, block settings, MIDI mappings and metronome settings. File references store the path and the content hash.
- **Preset files** (`.namsayin`): a zip containing `preset.json` (the chain, its MIDI map and the global sound controls) and, when exported with "embed files" on (default on), the NAM files, IRs and sidecar images it uses, under `files/<content hash>/`. Save and Save As keep presets in the preset folder (`presets/` in the config dir, with subfolders), without files, through an in-app dialog; Export writes an embedded copy anywhere through the system dialog; Open File… opens one from anywhere. Manage Presets… loads saved presets and marks one as the default, which new plugin instances load. The top menu has New, Save, Save As, Manage Presets…, Open File…, Export…, the embed option, and every saved preset laid out as the preset folder is: top-level presets first, then each folder as an indented heading with its presets and subfolders under it (the open preset in the highlight colour, the default marked). It scrolls when it's taller than the window. The preset name shows in the top bar, with a dot when the chain has changed since it was saved or opened.
- **Missing files**: on load, each block's file is looked for at its original path, then as an embedded copy (extracted to the cache), then in the library by content hash. If none is found, the block is created grayed out (passes audio through) with "Locate…", and "Use … from the library" when a library file with the same hash appears later.
- **Settings and favorites**: one JSON file per user — `~/.config/namsayin/settings.json` on Linux, `%APPDATA%\NAMsayin\settings.json` on Windows. It's written atomically, and every open instance reloads it when it changes (the UI checks its modification time once a second; a change re-reads the file before saving, so instances don't overwrite each other's changes). Fields: NAM folder, IR folder, default A2 size, normalization default for new blocks, multi-core splits, accent colour (stored as an index into the colour list, which keeps its order; Settings shows blue, the default, first), favorites. A `blockArt` key from older versions is ignored.
- **Favorites** are stored by content hash (XXH3-128 of the file bytes).
- **Undo/redo**: a stack of `ChainState` snapshots (small, so snapshots are cheap), up to 200 steps. One knob gesture is one step. It covers blocks, block values, bypass, the MIDI map and preset loads, not the global knobs. Undo/Redo buttons in the top bar; Ctrl+Z, Ctrl+Shift+Z and Ctrl+Y while the window has focus.

## 6. Library

- Scans the NAM directory and the IR directory on a background thread (`LibraryScanner`), including inside `.zip` files. Files inside zips are addressed as `archive.zip!/folder/model.nam`. When a block uses one, the file is extracted to the cache directory (`cache/extracted/`) off the audio thread and loaded from there.
- An index cache per kind (path, size, modification time, hash, metadata) keeps rescans fast. Only new or changed files are read and hashed. A rescan runs when the UI opens, when a folder changes, and from the Rescan button.
- NAM metadata (gear make/model/type) is shown under each model in the list.
- **Sidecar images**: for `My Amp.nam`, it looks for `My Amp.png`, then `.jpg`, then `.jpeg` next to it (inside zips too). They're shown on the tile and in the block panel; otherwise the block type's built-in image (`resources/blocks/<typeId>.*`, embedded by `tools/embed_block_images.py`), otherwise the placeholder art (`drawBlockArt()`: the Neon style, black enclosures outlined in glowing lines of the category colour; each pedal has its own symbol on its face, drawn from its name and purpose: `src/ui/BlockGlyphs.*`, plain polylines and dots in the pedal's 124x200 frame, so `tests/BlockGlyphTests.cpp` can check that every pedal has one and no two are the same; a type without one falls back to its category's symbol). 8-bit PNG or JPEG only (DPF's stb_image doesn't decode 16-bit PNG).
- **Add Block → NAM / IR** lists the library: a favorites filter, a star per file, and Browse… for a file outside the library. The search box at the top of the dialog (focused when it opens, × to clear) searches every category: built-in blocks by name and category, library files by name, folder or metadata (all words must match). Results are listed under category headings.

## 7. User interface

- DPF UI with NanoVG drawing. Resizable, with HiDPI scaling. The default logical size is ~1200 × 720 and the minimum is ~900 × 560.
- Layout:
  - **Top bar**: preset menu and preset name, undo/redo; on the right, icon buttons for the Fretbored link (the Fretbored app icon, drawn flat in the icon colour from its `assets/icon.svg`), the metronome (with ▾ for its panel), the tuner, settings and global bypass.
  - **Middle**: the chain, left to right, with square block tiles and "+" boxes. A split shows as two rows that rejoin at the merge. Hovering the gap between two blocks shows two small buttons: insert a block there (upper half) and, with no split yet, split there (lower half). A chain wider than the window scrolls with the mouse wheel, a thin scroll bar under it, or a middle-drag anywhere over it. Right-clicking a block offers Replace Block…, which opens Add Block and puts the new block in its place and slot (one undo step).
  - **Below the chain**: the selected block's panel: a large block image on the left (placeholder art until real pedal/amp/cab images exist), the name and file above the controls, and the generic controls right-justified in one or more rows. The generic knobs may later go away in favour of turning knobs on the image itself.
  - **Bottom bar**: mono/stereo toggle, input, bass, middle, treble and output knobs, input/output meters with clip lights (click to reset), limiter on/off.
- **Interactions**:
  - Drag a block to move it; drop indicators show where it lands.
  - Right-click a block: bypass, remove, MIDI learn.
  - Double-click a knob to reset it; Shift-drag for fine control.
- **Dialogs**: Add Block (categories on the left), Settings, and the Tuner and Metronome panels (overlays).
- **Redraws on Linux**: X11 events are only read when the host calls the UI. LV2 hosts call it on their idle tick (Reaper: ~30-36 Hz), so dragging redraws at that rate. For VST3, a DPF patch (`patches/dpf/`) registers the X11 connection with the host run loop's event handler, so events are handled and redrawn as they arrive, whatever the host's timer rate. The UI turns vsync off for its GL window, so a buffer swap never stalls the host's GUI thread waiting for the screen. `NAMSAYIN_SHOW_FPS=1` in the host's environment shows a frame-rate readout.
- **Hover help**: resting the mouse on a control for 0.6 s shows a translucent popup with its name and a plain-English explanation of what it does and how to use it. The texts are in `src/ui/ParamHelp.cpp` (block controls by typeId and control name, the others by parameter symbol); `tests/ParamHelpTests.cpp` checks that every block control and every global control has one, at least 80 characters long. The top-bar icon buttons and the metronome's beat, grouping and tap controls have popups too, and so does every block type (`blockTypeHelp()`: what it is, what it's for, how and where to use it), on its tile in the chain and on its entry in Add Block (categories and search results).
- A dark theme defined as colour tokens in one file, so it can be restyled: the window background is the owner's dark wood image, scaled to cover the window (only seen between panels); boxes are black with 1 px gray outlines (`surface()`), and buttons that are on are an accent outline over a faint accent tint. Knobs: hairline body, 21 ticks lit up to the value, a dot for the position. Font: Jost Light (Regular for small text), drawn 1.69x the layout size because Jost runs small, with DPF's DejaVu as the fallback for glyphs Jost lacks (★, ▾).
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
| M3 | **Delay, reverb and compressor** | The three blocks with their parameters, stereo delay and tempo sync |
| M4 | **Main UI** | Chain view with tiles, drag-and-drop, "+" and the Add Block dialog, block controls, bottom bar with knobs and meters, generic images, dark theme |
| M5 | **Library and settings** | Settings panel; NAM directory scanning with zip support, rescan button, index cache; hashing and favorites; sidecar images |
| M6 | **Split and merge** | One split with merge controls, latency alignment, branch B on a second core |
| M7 | **Presets, undo/redo, MIDI learn** | Preset menu, embedding, missing-file handling, undo/redo, MIDI learn |
| M8 | **Tuner** | Traditional and strobe views, mute, reference pitch, accuracy target met in tests |
| M9 | **Metronome** | Full Fretbored feature set, free and DAW-sync modes |
| P | **Pedals** (owner's request, before v1) | The pedal list in Requirements, with tests (section 4.11) |
| M10 | **v1.0 release** | Performance targets met, DAW testing done, docs, first GitHub Release |

M2–M3 and M4 can overlap, as can M8 and M9.

## 11. Decisions log

1. **CPU baseline**: AVX2/FMA is required.
2. **Standalone app**: dropped for now; VST3 and LV2 only.
3. **Reverb types**: room, hall and plate.
4. **Test DAW**: Reaper is the primary host.
5. **Compressor**: added to M3, studio style (owner's choice over a simple pedal-style one).
6. **Theme**: no gradient; pure black window. Boxes were flat near-black gray at first; the owner then chose black boxes with hairline gray outlines, Jost Light and tick-scale knobs with a dot, from drafted screenshots (fonts, knobs and surfaces; 2026-10-22).
7. **Block panel**: large block image on the left, generic knobs right-justified.
8. **Folder choice**: DPF's file browser can't pick folders, so "Choose…" opens the system's folder picker (`src/ui/FolderPicker.*`, on its own thread): Linux tries the desktop portal (FileChooser v3, `directory`), then zenity, then kdialog; Windows uses IFileOpenDialog with FOS_PICKFOLDERS. With none of these, it falls back to DPF's browser and takes the folder of any file picked in it. The path can also be typed or pasted.
9. **MIDI map in the chain state** rather than a separate plugin state, so it follows the chain's routes (including the exchange file for Reaper's LV2) and is covered by undo and presets.
10. **Metronome samples**: only the Classic clicks come from Fretbored. Its Digital samples carry a third-party artist credit (owner's call to remove them), so the Digital sound is generated in code.
11. **16 parameters per block** (was 8), for the Doppelganger Delay. Most host parameter indices moved, which was fine only because nothing was released yet; from v1.0 on the count is fixed.
12. **Pedals before v1**: the owner asked for a wide pedal set first (no rush to v1). Varieties are a Type choice inside one block rather than separate blocks; a released Choice list never changes (saved values would map to other choices), so new varieties that don't fit become new blocks.
13. **Pitch effects without latency**: delay-line pitch shifting with pitch-synchronous splices (polyphonic, clean on notes and most chords) instead of FFT or PSOLA methods that add latency; the synth, harmonizer and octave track pitch with YIN on a decimated copy (detection takes 4-24 ms, but adds no latency to the audio).
14. **Looper controls as momentary parameters**, so footswitches map to them with MIDI learn; the looper keeps one loop (60 s at 48 kHz).

