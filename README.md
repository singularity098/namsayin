# NAMsayin

A free, open source guitar rig plugin (VST3 and LV2) built on [Neural Amp Modeler](https://github.com/sdatkinson/NeuralAmpModelerCore) and [DPF](https://github.com/DISTRHO/DPF). Runs on Linux and Windows (x86-64 with AVX2).

Status: pre-release. Load NAM models (A1 and A2) and cab IRs (.wav) from your library (folders and zips), add built-in pedals, chain them (with an optional parallel split), and shape the result with the global controls. Presets, undo/redo, MIDI learn, and a built-in tuner and metronome.

Built-in pedals: boost, overdrive, distortion, fuzz, bitcrusher; compressor, noise gate, volume, auto swell; graphic and parametric EQ; chorus, flanger, phaser, tremolo, rotary, doubler, ring mod; reverb, spring reverb, shimmer, freeze; delay, tape echo, ducking delay, looper; octave, poly octave, pitch shifter, whammy, harmonizer, synth; filter; wah and auto wah. Most come in several types.

![NAMsayin prototype](docs/images/prototype.png)

 See [docs/Requirements.md](docs/Requirements.md) and [docs/Architecture.md](docs/Architecture.md).

## Building

```sh
git clone --recursive https://github.com/singularity098/namsayin
cd namsayin
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Plugins are written to `build/bin/` (`namsayin.lv2`, `namsayin.vst3`).

Linux needs: a C++20 compiler, CMake 3.24+, and the OpenGL/X11 development packages
(`libgl1-mesa-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libdbus-1-dev` on Debian/Ubuntu).

Windows: Visual Studio 2022 — `cmake -S . -B build -G "Visual Studio 17 2022" -A x64` then `cmake --build build --config Release`.

## License

MIT — see [LICENSE](LICENSE).
