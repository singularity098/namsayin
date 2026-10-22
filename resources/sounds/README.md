# Metronome sounds

The Classic click samples (`high.wav`, `low.wav`), copied from [Fretbored](https://github.com/singularity098/fretbored)
(`assets/sounds/`), where the project owner supplied them. 16-bit mono, 44.1 kHz.

They're built into the plugin: after changing them, run `python3 tools/embed_sounds.py` from the repository root to
regenerate `src/engine/metronome/ClickSamples.cpp`.

The Digital and Beep sounds are generated in code (`src/engine/metronome/Metronome.cpp`). Only add samples here that
the owner has cleared for the MIT license.
