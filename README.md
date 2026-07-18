# MidiGenerator

A generative MIDI effect built on [iPlug2](https://iplug2.github.io/). It doesn't make sound itself -- it generates note, CC, and clock data for a synth or DAW downstream.

- **Rhythm**: Euclidean pattern generator (Steps/Pulses/Rotation/Invert), plus Density and Chaos (logistic-map) modes as live-evaluated alternatives. Trig Every/Offset and Rotation Drift add longer-form, non-random structure; Ratchet adds rapid single-note retriggers; Accent gives the rhythm a felt downbeat.
- **Harmony**: 20 chord types across 5 voicings, quantized to a chosen key/scale (14 scales -- the 7 diatonic modes plus harmonic/melodic minor and a set of exotic 7-note scales), with a slow scale-degree "harmony drift" wander whose direction can be biased toward the tonic (Drift Gravity) or left a pure random walk. Chord Priority optionally keeps chord tones exactly as voiced, unquantized.
- **Modulation**: 8 assignable CC mod slots driven by internal sources (velocity, gate, probability, drift, random), plus optional 24ppqn MIDI clock output for external gear.
- **Performance**: Freeze stops new hits and lets whatever's ringing continue; Reroll reseeds the generative wander to a fresh starting point without stopping playback.
- **Export**: renders the current pattern (optionally several independently-reseeded variations at once) to standalone MIDI file(s) for dropping into a DAW's arrange view.
- **Presets**: 5 factory presets covering distinct sonic identities (ambient pad, percussive/rhythmic, bass, generative chords, sparse bells).

WebView-based UI (`resources/web/`), with Full and Compact layouts.
