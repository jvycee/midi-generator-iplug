# Drift — User Guide & Test Reference

A generative MIDI effect. Every control below lists **what it does**, its
**default / range**, the **expected behavior**, and a **quick test**. A
final section lists known quirks so you can tell a real bug from intended
(if odd) behavior.

---

## Before anything: three things that trip everyone up

1. **Drift makes no sound by itself.** It's a MIDI *effect* — it outputs
   notes, CC, and clock. Put a synth/instrument *after* it in the chain and
   you'll hear that synth playing Drift's notes.
2. **It only generates while the host transport is playing.** Press play in
   your DAW. Stopped = silence (by design — it reads the host's musical
   position). If nothing happens, check transport first.
3. **All modulation slots ship Off.** Out of the box Drift sends notes only.
   CC output is opt-in (see *Modulation*). (This is the fix for the old
   "Drift knob changes the volume" surprise — that was a mod slot, not Drift.)

**Signal flow of one hit:** transport advances → the *rhythm* decides if this
16th-note step fires → *harmony* picks the note(s) → *note shaping* sets
length/velocity/timing → notes go out on the MIDI channel.

---

## Global (header bar)

### Freeze  ·  switch (Live / Frozen), default Live
Stops triggering **new** hits. Notes already sounding ring out and decay
normally; the generative state (random wander, chaos position) is paused,
not reset.
- **Test:** with a pattern playing, flip to Frozen — new notes stop, tails
  finish. Flip back — it resumes from where the wander left off (not from
  the start).

### Reroll  ·  button
Reseeds the generative wander: new random stream, harmony position back to
the tonic, fresh Chaos state. Use it to "roll a new take" of anything
random (Density, Chaos, Harmony Drift, Soft clock, Random mod).
- **Test:** in Density or Chaos mode, hit Reroll a few times — the pattern
  should jump to a different evolving sequence each time. In pure Euclidean
  mode with no drift/probability, Reroll has little audible effect (nothing
  random is driving it).

---

## Rhythm Generator

### Sequence Mode  ·  Euclidean / Density / Chaos, default Euclidean
Chooses how each step decides to fire:
- **Euclidean** — a fixed, evenly-spread pattern from Steps/Pulses/Rotation.
  Repeats exactly every loop.
- **Density** — ignores the Euclidean pattern; each step fires independently
  with probability = the Dens/Chaos knob. A shifting cloud that never
  repeats. *Steps still sets the loop length; Pulses/Rotation/Invert do
  nothing in this mode.*
- **Chaos** — a logistic-map sequence: deterministic but non-repeating. The
  Dens/Chaos knob sets how chaotic. Same caveat: Pulses/Rotation/Invert
  don't apply.
- **Test:** set Euclidean, note the repeating pattern. Switch to Density —
  now it's a random-feeling cloud whose thickness follows Dens/Chaos.
  Switch to Chaos — evolving but not random-sounding; Reroll changes its path.

### Steps  ·  1–32, default 16
Pattern length in 16th-notes (16 = one bar). Applies in **all** modes (it's
the loop length).

### Pulses  ·  1–32, default 5
Number of hits spread as evenly as possible across Steps (Euclidean mode).
Pulses ≥ Steps = every step fires. **Euclidean only.**
- **Test:** Steps 16, sweep Pulses 1→16 — hits fill in evenly, step 0 (the
  downbeat) always plays.

### Rotation  ·  0–31, default 0
Rotates the Euclidean pattern earlier (shifts hits left). **Euclidean only.**

### Pattern Invert  ·  switch (Normal / Inverted), default Normal
Flips which steps are active. E(5,16) inverted plays the 11 silent steps
instead. **Euclidean only.**
- **Test:** toggle it — the rhythm becomes the photo-negative of itself
  (every hit becomes a rest and vice-versa).

### Dens/Chaos  ·  0–100%, default 50%
The amount knob for Density and Chaos modes (unused in Euclidean).
- Density: higher = more steps fire.
- Chaos: higher = more chaotic.

### Probability  ·  0–100%, default 100%
Per-hit thinning applied *on top of* whichever mode fired. 100% = keep every
hit; lower = randomly drop some. Works in all modes.
- **Test:** any pattern, drop Probability to ~50% — roughly half the hits
  randomly vanish each loop.

### Swing  ·  0–100%, default 0%
Delays every off-beat (odd) 16th, tempo-relative. Classic swing feel.

### Trig Every  ·  1–8, default 1  &  Trig Offset  ·  0–7, default 0
Track-level "play once every N loops." Trig Every = 2 means the whole track
plays every *other* pass through the pattern; Trig Offset picks which pass.
Long-form drop-in/drop-out, independent of the per-hit randomness.
- **Test:** Trig Every 2 — the pattern plays one loop, rests one loop,
  repeat. Offset 1 flips which loop it plays on.

### Rotation Drift  ·  0–128, default 0 (off)
Every N 16th-notes, the pattern's effective rotation auto-advances one step
and wraps. The pattern's phase slowly *crawls* against the downbeat without
any randomness. Adds on top of the manual Rotation knob. Pick a period that
doesn't divide Steps evenly (e.g. 24 against 16 steps) for long phasing.
- **Test:** Euclidean pattern, set Rotation Drift to ~24 — over many bars the
  hits slowly slide around the bar and only realign after a long cycle.
  **Euclidean only.**

### Ratchet  ·  1–8, default 1 (off)
Retriggers a hit several times in a row (drum-roll / stutter). Spread across
up to a full beat (or the note's length if shorter), so low values are gentle
and only high values approach a fast roll.
- **Only affects hits that resolve to a single note** — mono mode, or a chord
  that happens to collapse to one pitch. On a real chord it does nothing.
- **Test:** turn on Mono, set Ratchet 4 — each hit becomes four evenly-spaced
  re-attacks. Turn Mono off with a chord — Ratchet should have no effect.

### Accent Every  ·  1–8, default 4  &  Accent Amount  ·  0–100%, default 0% (off)
Boosts velocity on every Nth step for a felt downbeat. Amount 0 = fully off
regardless of Every. Counts steps within the pattern (step 0, N, 2N…).
- **Test:** Accent Every 4, Accent ~40% — every 4th step hits noticeably
  harder. At 0% there's no accent.

### Internal Clock Align  ·  Hard / Soft, default Hard  &  Soft Amount  ·  0–100%, default 25%
- **Hard** — note-ons land exactly on the grid.
- **Soft** — each hit gets a small random late nudge (humanize), scaled by
  Soft Amount. The Soft Amount slider only matters in Soft mode.
- **Test:** Soft + high Soft Amount — timing feels loose/human. Reroll
  changes the exact nudges.

---

## Harmony & Voicing

### Key  ·  C…B, default C  &  Scale  ·  14 scales, default Ionian
The key and scale everything is pulled into. Scales: the 7 diatonic modes
(Ionian…Locrian) plus Harmonic Minor, Melodic Minor, Byzantine, Persian,
Neapolitan Minor, Neapolitan Major, Hungarian Minor.
- **Test:** hold a pattern, change Scale — the same rhythm re-colors into the
  new scale's mood (try Phrygian or Byzantine for obvious character).

### Root  ·  MIDI note, default C4 (60)
The register/anchor the notes are built from. **Independent of Key** — a Root
outside the Key still gets pulled onto the Key/Scale (unless Chord Priority
is on). Think of Root as "where on the keyboard," Key/Scale as "which notes."
- **Test:** move Root up/down an octave — the whole output shifts register.

### Chord Type  ·  20 types, default Min9  &  Voices  ·  1–6, default 4
Chord quality (Single, Power, Octave, Major…Dim7) and how many notes.
- **Gotcha (not a bug):** if Voices asks for more notes than the chord has,
  the extras are **octave doublings**, not added 9ths/11ths. For real added
  tensions pick an already-extended type (Maj9, Dom9, Min11…).
- **Test:** Chord Type Major, Voices 1→6 — you get the triad then octave
  doublings. Chord Type Min11, Voices 6 — you get the actual stacked tensions.

### Voicing  ·  Close / Drop2 / Drop3 / Spread / Wide, default Close
How the chord tones are spread. **Heads-up: "Wide" currently behaves exactly
like "Close"** (its old octave-spread was dead code and was removed) — that's
a known no-op, not your ears. Close/Drop2/Drop3/Spread all differ audibly.

### Chord Priority  ·  Scale / Chord, default Scale
- **Scale** (default) — chord tones are snapped into the Key/Scale. Nothing
  clashes with the key, but a chord's altered tones (a b7, a #5) may bend
  into a different chord.
- **Chord** — the chord keeps its exact intervals, even if that steps outside
  the key. Guarantees the chord you picked; may add out-of-key color.
- **Test:** Key C Ionian, Chord Type Dom7, Voicing Close. In Scale mode the
  b7 (Bb) gets bent onto the scale; in Chord mode the Bb survives — you'll
  hear the dominant-7th "bite" appear.

### Note Length  ·  1–256 steps, default 32  &  Gate Time  ·  0–100%, default 85%
- **Note Length** — how long a note *can* ring, in 16th-note steps. 32 = two
  bars. This is why pads sustain across bars.
- **Gate** — fraction of that span actually held before release.
- **Important interaction (not a bug):** long Note Length + dense hits means
  many notes overlap and ring at once. Once you exceed **Max Voices**, the
  oldest notes get cut to make room — if notes seem to stop early, that's
  voice-stealing, not a dropout. Raise Max Voices, lower Voices, or thin the
  rhythm.

### Monophonic Output  ·  On / Off, default Off
On = every hit is a single scale-quantized note (no chord math). For bass,
leads, and mono synths. Also the mode where Ratchet works.

---

## Modulation & Utility

### Velocity  ·  0–100%, default 60%
Base velocity. A subtle *downward-only* humanize is always applied (hits are
sometimes softer, never louder than the knob) so pads breathe. Accent can
push individual steps above this.

### MIDI Channel  ·  1–16, default 1
Output channel. (Drift sends plain MIDI on one channel — no MPE.)

### Transpose  ·  −24…+24 semitones, default 0
Global transpose applied after harmony, folded back into MIDI range.

### Max Voices  ·  1–16, default 8
Global cap on simultaneously-ringing notes across all overlapping hits (not
per-chord). See the Note Length interaction above — this is the knob that
controls voice-stealing.

### Mod 1–8  ·  Source + CC#, all Sources default **Off**
Eight assignable CC/CV send slots for driving an external synth or modular
rig. Each picks a **Source** (a live track value) and a **CC number** to send
it as. Sources: Off, Velocity, Gate, Probability, Dens/Chaos, Drift, Random.
- Sent both continuously (~50 Hz) and on each hit, so it behaves like CV.
- **All slots ship Off** — nothing is sent until you assign one. (Old default
  routed Drift → CC74 = filter cutoff, which read as "Drift changes volume.")
- **Note:** most Sources (Drift, Probability, Dens/Chaos, Gate, Velocity) are
  *knob values* — sending one is really "expose this knob as a CC," roughly
  constant until you move it. Only **Random** moves continuously on its own.
- **Test:** set Mod 1 = Velocity, CC 1; MIDI-learn CC1 on your synth — it now
  tracks each note's velocity. Set Mod 1 = Random for a wandering CC.

### Send MIDI Clock  ·  On / Off, default Off
Sends standard 24 ppqn MIDI clock plus Start/Stop, for external gear to sync
to the host tempo. Off unless you need hardware sync.

---

## Export (drag to DAW)

### Length  ·  1–16 bars, default 4  &  Variations  ·  1–8, default 1
### Drag Generated MIDI  ·  button
Renders the current settings to a `.mid` file you can drag into your DAW's
arrange/piano-roll as a real, editable region.
- **Length** — how many bars to render.
- **Variations** — render several independently-reseeded takes at once
  (1 = a single file). Variation 1 is always the current live state; the rest
  are fresh rerolls.
- On macOS the file(s) are also revealed in Finder as a fallback.
- **Test:** set Length 4, Variations 3, click — you get 3 numbered `.mid`
  files, each a different take of the same settings. The export should match
  what you hear live (repeated pitches don't stack — they're cleanly cut).

---

## Compact view — read this, it's the biggest surprise

The **Compact** button swaps to a stripped-down view with a big "Chaos" knob
and a "Density" slider. **These are NOT the same as the Sequence-Mode/amount
params** — they're macro controls that remap underneath:

- **Compact "Chaos" knob** — does *not* select Chaos mode. It **randomizes
  Rotation** (by an amount that grows with the knob) and **lowers
  Probability**. Because it uses randomness, moving it scrambles things
  differently each time.
- **Compact "Density" slider** — a remote for **Pulses** (as a ratio of
  Steps).

So if you set things in Full view, switch to Compact, and nudge those
controls, they'll overwrite your Rotation/Probability/Pulses. That's
intended, but it *will* surprise you if you expect direct Chaos/Density
control. Use Full view for precise work.

---

## Known quirks & possible surprises (so you can tell bug from behavior)

- **No sound at all?** It's a MIDI effect — needs a synth after it, and the
  transport must be playing.
- **"Wide" voicing = "Close."** Known no-op (dead feature removed). Not a bug.
- **Notes cut off early with long Note Length?** Voice-stealing at Max Voices.
  Expected; raise Max Voices or thin the rhythm.
- **Ratchet does nothing?** It only affects single-note hits — turn on Mono,
  or use a hit that collapses to one pitch.
- **Compact Chaos/Density knobs overwrite Rotation/Probability/Pulses.** By
  design (see above).
- **Voices beyond the chord's size = octave doublings, not new tensions.**
  Pick an extended chord type for real 9ths/11ths.
- **Reroll seems to do nothing?** You're in a fully-deterministic setup
  (Euclidean, no Probability/Drift/Soft/Random). Nothing random to reseed.
- **Changing a param mid-session doesn't move your current instance's mod
  slots.** Defaults only apply to *new* instances/presets; saved state keeps
  its values.
- **At extreme tempo + very large buffers**, catch-up for skipped 16ths is
  capped (to avoid note storms on transport jumps); you *shouldn't* hear
  dropped steps at normal settings, but flag it if you do.
- **Real DAW drag-and-drop of the exported `.mid`** relies on a WebView drag
  flavor that isn't verified in every host; the Finder-reveal (macOS) is the
  reliable fallback. There's no reveal on iOS.

---

## Suggested test pass (10 minutes)

1. Load Drift → synth, press play. Confirm you hear notes (Euclidean 5/16).
2. Rhythm: sweep Pulses, Rotation; toggle Invert; try Density then Chaos;
   drop Probability; add Swing.
3. Trig Every 2 (drop-in/out); Rotation Drift ~24 (slow phasing).
4. Harmony: change Key/Scale (try Byzantine); move Root an octave; Chord Type
   Min11 with Voices 6; toggle Chord Priority on a Dom7 in C.
5. Shaping: long Note Length + high Gate (pad); then Mono + Ratchet 4; add
   Accent Every 4 @ 40%.
6. Utility: Mod 1 = Random → CC1, MIDI-learn on the synth; confirm Drift knob
   itself does **not** change level.
7. Export: Length 4, Variations 3 — drag the files in, compare to live.
8. Compact view: confirm the Chaos/Density macros behave as documented.

Note anything that doesn't match the "expected behavior" above — that's the
surprise list we want.
