# Metronome sounds

The Classic (`high.wav`, `low.wav`) and Digital (`digital-high.wav`, `digital-low.wav`) click samples, copied from
[Fretbored](https://github.com/singularity098/fretbored) (`assets/sounds/`), where the project owner supplied them.
16-bit mono, 44.1 kHz.

They're built into the plugin: after changing them, run `python3 tools/embed_sounds.py` from the repository root to
regenerate `src/engine/metronome/ClickSamples.cpp`.
