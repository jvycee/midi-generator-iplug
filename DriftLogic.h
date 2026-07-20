#pragma once

#include <array>
#include <algorithm>   // std::sort / std::unique / std::clamp
#include <cstdint>
#include <cmath>
// Removed juce_core to allow standalone testing

//==============================================================================
// 1. BRESENHAM EUCLIDEAN ALGORITHM
//
// Note: this is the Bresenham/"line-drawing" construction, not Bjorklund's
// recursive one. Both produce maximally-even patterns; they can differ by a
// rotation. With the accumulator seeded (below) this matches canonical
// Bjorklund exactly for E(4,16), E(3,8), E(7,16), E(2,4) and differs only by
// rotation for E(5,16) -- which the per-track rotation control covers.
//
// Patterns are carried as a uint32_t bitmask (bit i = step i fires), not a
// std::vector<bool>: kParamSteps is capped at 32, so a fixed-width int holds
// any pattern this plugin can produce with zero heap traffic.
//==============================================================================
inline uint32_t computeEuclidean(int pulses, int steps)
{
    steps = std::clamp(steps, 0, 32);
    if (steps <= 0 || pulses <= 0) return 0;
    if (pulses >= steps) return steps == 32 ? 0xFFFFFFFFu : ((1u << steps) - 1u);

    uint32_t result = 0;

    // FIX: seeding the accumulator at (steps - pulses) makes step 0 the first
    // pulse. Starting at 0 meant the accumulator could never reach `steps` on
    // the first iteration, so step 0 was structurally silent for every pattern
    // with pulses < steps -- E(4,16) came out as ...x...x...x...x rather than
    // x...x...x...x..., i.e. every pattern played off the downbeat.
    int bucket = steps - pulses;

    for (int i = 0; i < steps; ++i)
    {
        bucket += pulses;
        if (bucket >= steps)
        {
            bucket -= steps;
            result |= (1u << i);
        }
    }
    return result;
}

// Positive rotation shifts hits *earlier* (pattern moves left). If you'd rather
// the knob push hits later, negate `rotation` at the call site in
// rebuildTrackPattern rather than here, so saved presets keep their meaning.
inline uint32_t rotatePattern(uint32_t bits, int steps, int rotation)
{
    if (steps <= 1) return bits;
    rotation = ((rotation % steps) + steps) % steps;
    if (rotation == 0) return bits;

    uint32_t mask = (steps >= 32) ? 0xFFFFFFFFu : ((1u << steps) - 1u);
    bits &= mask;
    // n-bit rotate-right by `rotation`: new[i] = old[(i + rotation) % steps],
    // matching the original std::rotate(begin, begin + rotation, end).
    return ((bits >> rotation) | (bits << (steps - rotation))) & mask;
}

//==============================================================================
// 2. CHORD ENGINE
//==============================================================================
enum class ChordType : int
{
    SingleNote  = 0,
    Power       = 1,
    Octave      = 2,
    Major       = 3,
    Minor       = 4,
    Diminished  = 5,
    Augmented   = 6,
    Sus2        = 7,
    Sus4        = 8,
    Major7th    = 9,
    Minor7th    = 10,
    Dominant7th = 11,
    Minor9th    = 12,
    Major9th    = 13,
    Dom9th      = 14,
    Minor11th   = 15,
    Major6th    = 16,
    Minor6th    = 17,
    HalfDim7    = 18,
    Dim7        = 19,
    Count
};

inline const std::array<int, 6>& getChordIntervalsFixed(ChordType type, int& outCount)
{
    // Fixed-width table (max 6 intervals for Min11) -- no heap, matches the
    // fixed-capacity chord math below.
    static const struct { std::array<int, 6> iv; int n; } table[(size_t)ChordType::Count] =
    {
        { {0,0,0,0,0,0}, 1 }, { {0,7,0,0,0,0}, 2 }, { {0,12,0,0,0,0}, 2 },
        { {0,4,7,0,0,0}, 3 }, { {0,3,7,0,0,0}, 3 }, { {0,3,6,0,0,0}, 3 },
        { {0,4,8,0,0,0}, 3 }, { {0,2,7,0,0,0}, 3 }, { {0,5,7,0,0,0}, 3 },
        { {0,4,7,11,0,0}, 4 }, { {0,3,7,10,0,0}, 4 }, { {0,4,7,10,0,0}, 4 },
        { {0,3,7,10,14,0}, 5 }, { {0,4,7,11,14,0}, 5 }, { {0,4,7,10,14,0}, 5 },
        { {0,3,7,10,14,17}, 6 }, { {0,4,7,9,0,0}, 4 }, { {0,3,7,9,0,0}, 4 },
        { {0,3,6,10,0,0}, 4 }, { {0,3,6,9,0,0}, 4 },
    };
    const auto& e = table[(size_t)type];
    outCount = e.n;
    return e.iv;
}

inline int getChordTypeCount() { return (int)ChordType::Count; }

enum class VoicingStyle : int
{
    Close  = 0,
    Drop2  = 1,
    Drop3  = 2,
    Spread = 3,
    Wide   = 4,
    Count
};

// Arpeggiator: orthogonal to SequenceMode (which decides WHEN a step fires)
// and to Voicing (which still fully applies -- it reorders/spreads the
// chord before the arp steps through it, so the same direction sounds
// different under Close vs Drop2 vs Spread, for free). Off (default) is the
// existing "fire the whole chord at once" behavior, unchanged. Scoped to
// Up/Down/Up-Down for v1 -- Down-Up is only meaningfully distinct from
// Up-Down at reset time given the cursor is persistent track state, and
// "As Played" has no coherent meaning here since chords are generated, not
// received from live note input.
enum class ArpMode : int
{
    Off = 0,
    Up,
    Down,
    UpDown,
    Count
};

// FIX: fold out-of-range notes by octaves instead of clamping to 0/127.
// jlimit() collapsed every over-range voice onto the same pitch, which both
// destroyed the chord and created duplicate note-ons -- the first note-off
// then killed all copies.
inline int foldIntoMidiRange(int n)
{
    while (n > 127) n -= 12;
    while (n < 0)   n += 12;
    return n;
}

//==============================================================================
// NoteSet: fixed-capacity replacement for std::vector<int> as the return type
// for chord-building. Every hit this plugin can produce tops out at 8 notes
// (6-voice chord cap plus headroom for the octave-doubling pad below), so a
// stack array with a count sidesteps a heap allocation per triggered step --
// buildTrackNotes() is called from ProcessBlock, i.e. the audio thread.
//==============================================================================
constexpr int kMaxChordNotes = 8;

struct NoteSet
{
    int notes[kMaxChordNotes];
    int count = 0;

    void push(int n) { if (count < kMaxChordNotes) notes[count++] = n; }
    int*       begin()       { return notes; }
    int*       end()         { return notes + count; }
    const int* begin() const { return notes; }
    const int* end()   const { return notes + count; }
};

inline NoteSet buildChord(int rootNote, ChordType type, VoicingStyle voicing, int numNotes)
{
    numNotes = std::clamp(numNotes, 1, kMaxChordNotes);

    int intervalCount = 0;
    const auto& intervals = getChordIntervalsFixed(type, intervalCount);

    int raw[kMaxChordNotes];
    for (int i = 0; i < intervalCount; ++i)
        raw[i] = rootNote + intervals[i];
    const int origCount = intervalCount;
    int count = intervalCount;

    if (numNotes <= 1)
    {
        NoteSet single; single.push(raw[0]); return single;
    }

    if (numNotes <= origCount)
        count = numNotes;
    else
        while (count < numNotes)
        {
            raw[count] = raw[count - origCount] + 12;
            ++count;
        }

    switch (voicing)
    {
    case VoicingStyle::Close:
        break;

    case VoicingStyle::Drop2:
        if (count >= 4) raw[count - 2] -= 12;
        break;

    case VoicingStyle::Drop3:
        if (count >= 4) raw[count - 3] -= 12;
        break;

    case VoicingStyle::Spread:
        // Fans voices outward alternately. Deliberately extreme: with 6 voices
        // this spans ~6 octaves, so the fold below does real work here.
        for (int i = 0; i < count; ++i)
        {
            if (i % 2 == 1) raw[i] += 12 * ((i / 2) + 1);
            else if (i > 0) raw[i] -= 12 * (i / 2);
        }
        break;

    case VoicingStyle::Wide:
        // Wide's only distinguishing behavior was an octave-spread driven by
        // spreadOctaves, which was never wired to a parameter (always 0) --
        // dead code, removed. Wide is currently identical to Close.
        break;

    default:
        break;
    }

    for (int i = 0; i < count; ++i)
        raw[i] = foldIntoMidiRange(raw[i]);

    std::sort(raw, raw + count);

    // FIX: collapse duplicates. Two note-ons for the same pitch on the same
    // channel means the first note-off releases both -- and the second is
    // orphaned. (A MIDI chord is a set of pitches; sorting here is harmless.)
    count = (int)(std::unique(raw, raw + count) - raw);

    NoteSet result;
    for (int i = 0; i < count; ++i)
        result.push(raw[i]);
    return result;
}

//==============================================================================
// 2b. KEY & SCALE ENGINE
//
// ChordType/VoicingStyle above stay purely chromatic (interval math off a
// root). Key + ScaleMode sit on top as a harmonic filter: whatever root and
// chord quality the user dials in gets pulled back onto the chosen scale, so
// "set a key, pick maj/min/sus/dim etc" compose freely instead of being two
// conflicting sources of pitch.
//==============================================================================
enum class ScaleMode : int
{
    Ionian = 0,     // Major
    Dorian,
    Phrygian,
    Lydian,
    Mixolydian,
    Aeolian,        // Natural minor
    Locrian,
    // Appended after the 7 diatonic modes so no existing value shifts.
    // All still 7-note scales, so they need no changes to the fixed-7
    // scale-degree machinery (scaleDegreeToNote, harmony drift's mod-7
    // wrap, quantizeToScale) -- just a different interval set.
    HarmonicMinor,
    MelodicMinor,     // ascending form
    Byzantine,        // aka Double Harmonic Major
    Persian,
    NeapolitanMinor,
    NeapolitanMajor,
    HungarianMinor,   // aka Gypsy Minor
    Count
};

inline int getScaleModeCount() { return (int)ScaleMode::Count; }

inline const std::array<int, 7>& getScaleIntervals(ScaleMode m)
{
    static const std::array<std::array<int, 7>, (size_t)ScaleMode::Count> table = {{
        {0, 2, 4, 5, 7, 9, 11},  // Ionian
        {0, 2, 3, 5, 7, 9, 10},  // Dorian
        {0, 1, 3, 5, 7, 8, 10},  // Phrygian
        {0, 2, 4, 6, 7, 9, 11},  // Lydian
        {0, 2, 4, 5, 7, 9, 10},  // Mixolydian
        {0, 2, 3, 5, 7, 8, 10},  // Aeolian
        {0, 1, 3, 5, 6, 8, 10},  // Locrian
        {0, 2, 3, 5, 7, 8, 11},  // HarmonicMinor (natural minor with a raised 7th)
        {0, 2, 3, 5, 7, 9, 11},  // MelodicMinor, ascending (natural minor with a raised 6th and 7th)
        {0, 1, 4, 5, 7, 8, 11},  // Byzantine / Double Harmonic Major
        {0, 1, 4, 5, 6, 8, 11},  // Persian
        {0, 1, 3, 5, 7, 8, 11},  // NeapolitanMinor
        {0, 1, 3, 5, 7, 9, 11},  // NeapolitanMajor
        {0, 2, 3, 6, 7, 8, 11},  // HungarianMinor / Gypsy Minor
    }};
    return table[(size_t)m];
}

// Nearest in-scale MIDI note to `note` (searches outward by semitone; every
// mode here has at least one scale tone within a tritone of any pitch class).
inline int quantizeToScale(int note, int key, ScaleMode mode)
{
    bool inScale[12] = {};
    for (int iv : getScaleIntervals(mode))
        inScale[iv] = true;

    int pitchClass = ((note - key) % 12 + 12) % 12;
    if (inScale[pitchClass]) return note;

    for (int d = 1; d <= 6; ++d)
    {
        if (inScale[(pitchClass + d) % 12])      return note + d;
        if (inScale[(pitchClass - d + 12) % 12]) return note - d;
    }
    return note; // unreachable for these 7-note modes
}

// MIDI note for scale degree `degreeIndex` (any integer -- wraps through
// octaves), kept within a tritone of `referenceNote` so a wandering degree
// index doesn't walk the register away from where the track was tuned.
inline int scaleDegreeToNote(int key, ScaleMode mode, int degreeIndex, int referenceNote)
{
    const auto& intervals = getScaleIntervals(mode);
    int idx = ((degreeIndex % 7) + 7) % 7;
    int octaveShift = (degreeIndex - idx) / 7; // exact: degreeIndex - idx is always a multiple of 7

    int note = key + intervals[idx] + 12 * octaveShift;
    while (note < referenceNote - 6) note += 12;
    while (note > referenceNote + 6) note -= 12;
    return note;
}

// Builds a chord as buildChord() does, then (when quantizeToScaleEnabled)
// pulls every tone back onto the chosen key/scale. Re-sorts and re-dedupes
// afterward: independent quantization can collapse two chord tones onto the
// same pitch (e.g. a diminished 5th snapping to the same scale step as its
// neighbor), and two note-ons for the same pitch is the duplicate-note-off
// hazard buildChord() already guards against above.
//
// quantizeToScaleEnabled=false ("Chord Priority") skips that snapping
// entirely, so the chord comes out with the exact chromatic intervals its
// ChordType defines (a Dominant7th is really root+4+7+10) instead of
// whatever those tones bend into once forced onto the scale -- in most
// keys a chord's altered/chromatic tones (a b7, a #5, ...) simply aren't
// diatonic, so the default ("Scale Priority") quantization can silently
// turn the chord you picked into a different chord. Neither is "more
// correct": Scale Priority guarantees everything stays in-key at the cost
// of chord identity; Chord Priority guarantees chord identity at the cost
// of possibly stepping outside the key.
//
// That collapse used to just silently return fewer notes than `numNotes` --
// which is exactly why some hits come out noticeably quieter than others
// with everything else held constant (a 2-note dyad sums to a lot less
// signal than the 4-voice chord the next hit gets). To keep loudness
// consistent hit-to-hit, pad back up to `numNotes` by octave-doubling
// existing tones rather than leaving the chord thinned.
inline NoteSet buildChordInKey(int rootNote, ChordType type, VoicingStyle voicing,
                                int numNotes, int key, ScaleMode mode,
                                bool quantizeToScaleEnabled = true)
{
    NoteSet result = buildChord(rootNote, type, voicing, numNotes);

    for (int& n : result)
        n = quantizeToScaleEnabled ? foldIntoMidiRange(quantizeToScale(n, key, mode)) : foldIntoMidiRange(n);

    std::sort(result.begin(), result.end());
    result.count = (int)(std::unique(result.begin(), result.end()) - result.begin());

    if (result.count > 0 && result.count < numNotes)
    {
        // Index against the *current* (growing) size, not the original --
        // otherwise when the chord collapsed to a single tone, every pass
        // retries the exact same +12 doubling forever instead of stacking
        // successive octaves (+12, +24, +36, ...) to actually reach numNotes.
        int tries = 0;
        int maxTries = numNotes * 8;
        while (result.count < numNotes && result.count < kMaxChordNotes && tries < maxTries)
        {
            int doubled = foldIntoMidiRange(result.notes[tries % result.count] + 12);
            bool alreadyPresent = std::find(result.begin(), result.end(), doubled) != result.end();
            if (!alreadyPresent)
                result.push(doubled);
            ++tries;
        }
        std::sort(result.begin(), result.end());
    }

    return result;
}

//==============================================================================
// 3. SEQUENCE ENGINES
//
// Euclidean is a fixed, precomputed pattern (rhythmic structure). Density and
// Chaos are evaluated live, per step, so they keep evolving rather than
// looping a baked pattern -- suited to generative/ambient use where you want
// the sequence to never quite repeat.
//==============================================================================
enum class SequenceMode : int
{
    Euclidean = 0,
    Density   = 1,  // independent per-step trigger probability (Bernoulli cloud)
    Chaos     = 2,  // logistic-map iteration; deterministic but non-repeating
    Count
};

inline int getSequenceModeCount() { return (int)SequenceMode::Count; }

// Fast, allocation-free PRNG (xorshift32). Each track keeps its own state so
// tracks don't lock-step on the same random sequence.
inline uint32_t xorshift32(uint32_t& state)
{
    if (state == 0) state = 0x9E3779B9u; // xorshift is fixed at 0; guard against a zeroed seed.
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// Uniform float in [0, 1).
inline float randUnit01(uint32_t& state)
{
    return (float)(xorshift32(state) >> 8) * (1.0f / 16777216.0f); // 24 significant bits
}

//==============================================================================
// 4. EUCLIDEAN TRACK
//==============================================================================
struct EuclideanTrack
{
    int steps       = 16;
    int pulses      = 5;
    int rotation    = 0;
    int rootNote    = 60;

    // Flips active/inactive steps in the Euclidean pattern after rotation --
    // doubles the pattern vocabulary for free (E(pulses,steps) inverted is
    // E(steps-pulses,steps) up to rotation, but expressing it as a toggle on
    // top of the existing Pulses/Rotation knobs is simpler than making the
    // user retune both to get the complementary shape). Density/Chaos modes
    // don't use patternBits, so this only affects Euclidean mode.
    bool patternInvert = false;

    // Track-level trig condition: this track is only "live" once every
    // trigEvery passes through the pattern (loop 0-indexed, gated against
    // trigOffset). trigEvery=1 (default) means always live -- the existing
    // per-hit sequencing is untouched unless this is deliberately dialed in.
    // Independent of SequenceMode: a whole track dropping in/out on a slow,
    // fixed cadence is structured long-form evolution, distinct from (and
    // stackable with) Density/Chaos/harmony drift's per-hit randomness.
    int trigEvery  = 1; // 1-8
    int trigOffset = 0; // which of every trigEvery loops this plays on

    // Rotation Drift: every rotationDriftPeriod 16th-notes, the Euclidean
    // pattern's *effective* rotation (rotation + a live, time-derived offset)
    // advances by one step and wraps mod `steps`. 0 = off (the manual
    // Rotation knob is the only source of rotation, same as before this
    // existed). Deliberately NOT tied to hits or loop count the way trig
    // conditions/harmony drift are -- it's a fixed-cadence crawl independent
    // of both the rhythm's own length and its RNG, so the pattern's phase
    // against the downbeat slowly and predictably drifts without ever
    // needing a dice roll. Two periods that don't evenly divide each other
    // (e.g. 16 steps against a 24-step drift period) only fully realign
    // after their combined cycle -- long-form phasing, not randomness.
    int rotationDriftPeriod = 0;

    ChordType chordType     = ChordType::SingleNote;
    VoicingStyle voicing    = VoicingStyle::Close;
    int chordNotes          = 3;

    // false ("Scale Priority", default): every chord tone gets snapped onto
    // the current key/scale, same as before this existed -- guarantees
    // nothing clashes with the key, at the cost of possibly bending the
    // chord you picked into a different one. true ("Chord Priority"): the
    // chord keeps its exact chromatic intervals, unquantized -- see
    // buildChordInKey's comment for the full tradeoff.
    bool chordPriority = false;

    // When on, every hit is exactly the scale-quantized root note -- no
    // chord/voicing math at all. For lead lines, bass, and monophonic synth
    // patches, which want one note per hit and no ambiguity about which
    // chord tone gets sent to a synth that can only play one note anyway.
    bool monoMode = false;

    // Off (default): every hit fires the whole chord, unchanged. Otherwise:
    // each hit fires just the NEXT note in the chord instead of all of them
    // -- see ArpMode and buildTrackNotes. arpIndex/arpDirection are the
    // persistent cursor through the chord, advanced once per triggered hit
    // (mirrors scaleDegree's role for harmony drift). Ignored when monoMode
    // is on (a 1-note "chord" has nothing to step through).
    ArpMode arpMode  = ArpMode::Off;
    int arpIndex     = 0;
    int arpDirection = 1; // +1 or -1, for Up-Down's ping-pong

    float velocity      = 0.8f;

    // A note's duration is noteLengthSteps (in 16th-notes) times gate: gate
    // alone used to be capped at one step, which is why sparse pulses still
    // sounded short and staccato no matter how few of them there were --
    // there was no way to make an individual hit ring longer than a 16th
    // note. noteLengthSteps sets the actual sustain (in steps, so it can
    // span many bars); gate shapes how much of that span is held vs cut
    // short, same as before.
    int noteLengthSteps = 32; // 2 bars at 16 steps/bar
    float gate          = 0.85f;
    float probability   = 1.0f;

    // Retriggers a hit ratchetCount times, spread across up to a full beat
    // (4 steps, or the note's own length if shorter -- see ProcessBlock/
    // MidiExport's ratchetSpanSteps) instead of one sustained note-on. 1 =
    // off (unchanged behavior). Scoped to hits that resolve to a single note
    // (monoMode, or any chord voicing that happens to collapse to one note)
    // -- ratcheting a full chord would multiply note count by ratchetCount
    // per hit, straight into the polyphony cap; a rapid single-note burst is
    // the actually-useful case (drum-machine-style rolls) and never stresses
    // voice count regardless of ratchetCount, since retriggers are
    // sequential, not simultaneous. Overrides noteLengthSteps/gate for that
    // hit -- a ratchet is a burst confined to its span, not N copies of a
    // potentially many-bar sustain.
    int ratchetCount = 1;

    SequenceMode seqMode = SequenceMode::Euclidean;
    float density        = 0.5f;   // Density mode: chance any given step fires
    float chaosAmount    = 0.5f;   // Chaos mode: 0..1 -> logistic-map r in the chaotic band
    double chaosX         = 0.6180339887; // logistic-map state (golden-ratio conjugate avoids the 0.5 fixed point)
    uint32_t rngState     = 0x9E3779B9u;

    // Harmony drift: scaleDegree is the track's position in the current
    // key/scale (0 = tonic). driftAmount is the per-hit chance it steps by
    // one scale degree, wrapping within the octave -- a slow, non-repeating
    // wander through the mode rather than a fixed chord every time.
    int scaleDegree    = 0;
    float driftAmount  = 0.0f;

    // driftGravity biases which direction a drift step takes: 0 (default) is
    // an unweighted coin flip -- a pure random walk that can wander onto any
    // degree with equal likelihood and never has to resolve, which is
    // exactly what a lot of ambient/generative use wants. 1.0 always steps
    // toward the tonic by the shorter path (real voice-leading's "pull home"
    // tendency); values between are a tunable blend. Deliberately not a
    // fixed behavior either way -- the two are legitimately different goals,
    // not a bug on one side.
    float driftGravity = 0.0f;

    // Accent: boosts the velocity fraction by up to accentAmount (capped at
    // 1.0) every accentEvery-th step, so the rhythm has a felt downbeat
    // instead of every hit landing at the same dynamic level. amount<=0
    // (default) is fully off regardless of accentEvery.
    int accentEvery     = 4;
    float accentAmount  = 0.0f;

    // Heads-up: every track defaults to channel 1. Two tracks whose chords
    // overlap on a pitch will steal each other's note-offs. Consider
    // defaulting track N to channel N.
    int midiChannel = 1;
    int rateDiv     = 16;
    bool muted      = false;

    uint32_t patternBits = 0; // bit i set => step i fires (Euclidean mode only)
    int currentStep = -1;

    // Manual per-step overlay on top of the generated Euclidean pattern (see
    // the step grid UI, kMsgTagStepToggleRequest). stepOverrideMask bit i set
    // means step i's fire/no-fire decision is manually forced rather than
    // generated; stepOverrideValue's bit i is then the forced value. Never
    // touched by ReseedTrack/ReseedTrackDeterministic -- a manually-curated
    // groove is a deliberate edit, not part of the "generative wander" Reroll
    // resets. See computeEffectivePatternBits.
    uint32_t stepOverrideMask  = 0;
    uint32_t stepOverrideValue = 0;
};

// The Euclidean pattern actually in effect once manual step overrides are
// layered on top of the generated patternBits -- what evaluateStepTrigger
// reads and what the step grid UI shows. Bits outside [0, t.steps) are
// simply never read by anything, so no clamping is needed here even though
// stepOverrideMask/Value can carry stale bits from a since-shrunk step count.
inline uint32_t computeEffectivePatternBits(const EuclideanTrack& t)
{
    return (t.patternBits & ~t.stepOverrideMask) | (t.stepOverrideValue & t.stepOverrideMask);
}

// Advances t.arpIndex (and t.arpDirection for Up-Down's ping-pong) to the
// NEXT position for a chord of `chordSize` notes, per t.arpMode. Called once
// per triggered hit, after this hit's note has already been picked using the
// CURRENT (pre-advance) index -- so the first hit plays index 0, then this
// runs to set up index 1 for the next hit, etc.
inline void advanceArpCursor(EuclideanTrack& t, int chordSize)
{
    if (chordSize <= 1) { t.arpIndex = 0; return; }

    switch (t.arpMode)
    {
        case ArpMode::Up:
            t.arpIndex = (t.arpIndex + 1) % chordSize;
            break;

        case ArpMode::Down:
            t.arpIndex = ((t.arpIndex - 1) % chordSize + chordSize) % chordSize;
            break;

        case ArpMode::UpDown:
        {
            // Ping-pong across [0, chordSize-1] without repeating an endpoint
            // twice in a row: 0,1,2,3,2,1,0,1,2,3,... for a 4-note chord.
            t.arpIndex += t.arpDirection;
            if (t.arpIndex >= chordSize)     { t.arpIndex = std::max(0, chordSize - 2); t.arpDirection = -1; }
            else if (t.arpIndex < 0)         { t.arpIndex = std::min(1, chordSize - 1); t.arpDirection = 1; }
            break;
        }

        default:
            break;
    }
}

// Notes for one triggered hit. Mono mode bypasses the chord engine entirely
// and returns just the scale-quantized root -- see EuclideanTrack::monoMode.
// Non-const: when Arp mode is active, this also advances the arp cursor as a
// side effect of picking this hit's note -- mirrors how applyHarmonyDrift
// already mutates track state (scaleDegree) as a side effect of a hit.
inline NoteSet buildTrackNotes(EuclideanTrack& t, int scaleRoot, int key, ScaleMode mode)
{
    if (t.monoMode)
    {
        NoteSet single;
        single.push(foldIntoMidiRange(scaleRoot));
        return single;
    }

    NoteSet chord = buildChordInKey(scaleRoot, t.chordType, t.voicing, t.chordNotes, key, mode, !t.chordPriority);

    if (t.arpMode == ArpMode::Off || chord.count <= 1)
        return chord;

    int idx = ((t.arpIndex % chord.count) + chord.count) % chord.count; // defensive wrap
    NoteSet single;
    single.push(chord.notes[idx]);
    advanceArpCursor(t, chord.count);
    return single;
}

//==============================================================================
// 5. GLOBAL CONFIG
//==============================================================================
enum class ClockAlign : int
{
    Hard = 0,   // note-ons land exactly on the grid
    Soft = 1    // note-ons get a small, per-hit randomized late nudge off the grid
};

struct GlobalParams
{
    float globalSwing      = 0.0f;
    int   globalTranspose  = 0;
    bool  enabled          = true;

    ClockAlign clockAlign     = ClockAlign::Hard;
    float clockSoftAmount    = 0.25f;  // 0..1, max jitter as a fraction of a 16th note

    int key             = 0;                  // tonic pitch class, 0 = C
    ScaleMode scaleMode = ScaleMode::Ionian;
};

//==============================================================================
// 6. UTILITY
//==============================================================================
inline void rebuildTrackPattern(EuclideanTrack& t)
{
    // Build the whole pattern in a local and publish it to patternBits in a
    // single store at the end. This runs on the UI thread (from
    // OnParamChange) while the audio thread reads patternBits in
    // evaluateStepTrigger; writing it in stages (compute -> rotate -> invert)
    // would let a reader observe an intermediate, un-rotated pattern for one
    // step. A single aligned 32-bit store is seen whole -- the reader gets
    // either the old or the new complete pattern, never a half-built one.
    // (Fully memory-model-correct would make patternBits std::atomic, but that
    // ripples into the single-threaded test/export readers for no practical
    // gain on the platforms this targets, where the aligned store is atomic.)
    uint32_t bits = computeEuclidean(t.pulses, t.steps);
    bits = rotatePattern(bits, t.steps, t.rotation);

    if (t.patternInvert)
    {
        int steps = std::clamp(t.steps, 0, 32);
        uint32_t mask = (steps >= 32) ? 0xFFFFFFFFu : ((1u << steps) - 1u);
        bits = (~bits) & mask;
    }

    t.patternBits = bits;
}

// Given the last-processed 16th-note index and the current one, returns the
// first 16th index a block should (re)process so no boundary the block
// straddled gets dropped -- callers then run [result, currentStep]. Pure
// integer logic, factored out of ProcessBlock so it's unit-testable.
//
//  - lastStep < 0 (first step after transport start/reset): current only --
//    we can't know how long we were stopped, so don't invent catch-up steps.
//  - forward gap of 1..maxCatchup: catch up (lastStep+1 .. current).
//  - gap <= 0 (backward: a loop wrap or rewind) or gap > maxCatchup (a
//    transport locate): current only -- those advance the index too, but by
//    far more than a real buffer straddle, and catching them up would fire a
//    burst of notes for time that never actually elapsed.
inline int computeCatchupFirstStep(int lastStep, int currentStep, int maxCatchup)
{
    int gap = currentStep - lastStep;
    if (lastStep >= 0 && gap >= 1 && gap <= maxCatchup)
        return lastStep + 1;
    return currentStep;
}

// Gates the *entire track* on/off for the current loopIndex (which pass
// through the pattern this is -- callers derive it as an absolute step
// count divided by the pattern length, no separate counter needed). Cheap
// and side-effect-free, so callers should check this before
// evaluateStepTrigger and skip it entirely on a closed loop -- that also
// avoids burning RNG draws (Density/Chaos/probability) on loops that
// wouldn't sound anyway.
inline bool evaluateTrigCondition(const EuclideanTrack& t, int loopIndex)
{
    if (t.trigEvery <= 1) return true;
    int offset = ((t.trigOffset % t.trigEvery) + t.trigEvery) % t.trigEvery;
    int loop = ((loopIndex % t.trigEvery) + t.trigEvery) % t.trigEvery;
    return loop == offset;
}

// Decides whether `stepIndex` fires, for whichever SequenceMode the track is
// in, then thins the result by `probability`. This is the one place that
// reads `patternBits` for Euclidean mode and the only place `probability` (and
// the per-track RNG) gets used -- previously declared on the track but never
// consulted.
// rotationDriftSteps: caller-derived live rotation offset from Rotation
// Drift (see EuclideanTrack::rotationDriftPeriod) -- 0 when drift is off,
// reproducing the exact prior behavior. Applied as one extra rotation on
// top of the cached (already rotated+inverted) patternBits rather than
// baking it into the cache, since it changes every step regardless of
// whether Steps/Pulses/Rotation/Invert have -- rotatePattern() is cheap
// (a couple of shifts, no allocation), so redoing it live every step here
// costs nothing worth caching against.
inline bool evaluateStepTrigger(EuclideanTrack& t, int stepIndex, int rotationDriftSteps = 0)
{
    bool hit = false;

    switch (t.seqMode)
    {
        case SequenceMode::Euclidean:
        {
            int steps = std::max(1, t.steps);
            uint32_t effectiveBits = computeEffectivePatternBits(t);
            uint32_t bits = rotationDriftSteps != 0 ? rotatePattern(effectiveBits, steps, rotationDriftSteps) : effectiveBits;
            hit = (bits >> (stepIndex % steps)) & 1u;
            break;
        }

        case SequenceMode::Density:
            hit = randUnit01(t.rngState) < t.density;
            break;

        case SequenceMode::Chaos:
        {
            double amt = std::clamp((double)t.chaosAmount, 0.0, 1.0);
            double r = 3.57 + amt * (4.0 - 3.57); // 3.57.. is where the logistic map turns chaotic
            t.chaosX = r * t.chaosX * (1.0 - t.chaosX);
            // Guard against collapsing onto the 0 or 1 fixed points for r near the edges.
            if (t.chaosX < 1e-6 || t.chaosX > 1.0 - 1e-6)
                t.chaosX = 0.6180339887;
            hit = t.chaosX > 0.5;
            break;
        }

        default:
            break;
    }

    if (hit && t.probability < 1.0f)
        hit = randUnit01(t.rngState) < t.probability;

    return hit;
}

// Called once per triggered hit, before the chord for that hit is built.
// Rolls driftAmount and, on success, steps scaleDegree by +/-1 (wrapping
// within the octave) -- the source of the track's slow harmonic wander.
// driftGravity (0 = pure coin flip, 1 = always toward tonic) biases which
// of the two directions gets chosen -- see EuclideanTrack::driftGravity.
inline void applyHarmonyDrift(EuclideanTrack& t)
{
    if (t.driftAmount <= 0.0f) return;

    if (randUnit01(t.rngState) < t.driftAmount)
    {
        // Shorter path back to degree 0 within a 7-note octave: down from
        // 1-3, up (wrapping) from 4-6. At the tonic itself either direction
        // is equally "away", so gravity has nothing to bias toward.
        int towardTonic = (t.scaleDegree == 0)
                         ? ((randUnit01(t.rngState) < 0.5f) ? -1 : 1)
                         : (t.scaleDegree <= 3 ? -1 : 1);

        float pTowardTonic = 0.5f + 0.5f * std::clamp(t.driftGravity, 0.0f, 1.0f);
        int step = (randUnit01(t.rngState) < pTowardTonic) ? towardTonic : -towardTonic;
        t.scaleDegree = ((t.scaleDegree + step) % 7 + 7) % 7;
    }
}

// Boosts a velocity fraction (0-1) on an accented step, capped at 1.0 -- see
// EuclideanTrack::accentEvery/accentAmount. A no-op (returns velocityFraction
// unchanged) whenever accentAmount<=0 or accentEvery<=0, reproducing the
// exact pre-accent velocity.
inline float applyAccent(float velocityFraction, int stepIndex, int accentEvery, float accentAmount)
{
    if (accentAmount <= 0.0f || accentEvery <= 0) return velocityFraction;
    if ((stepIndex % accentEvery) != 0) return velocityFraction;
    return std::min(1.0f, velocityFraction + accentAmount);
}

// Sample offset to delay a hit's note-on by, given the current global 16th
// grid position. Two independent sources of offset, both additive and both
// tempo-relative (scaled by the 16th-note length, not an absolute time):
//  - swing: classic delay of every off-grid (odd) 16th
//  - soft clock alignment: a small per-hit random late nudge so the sequence
//    doesn't feel quantized, while still tracking the host tempo
inline int computeTimingOffsetSamples(EuclideanTrack& t, int gridPos16th,
                                       const GlobalParams& g, int sixteenthNoteSamples)
{
    int offset = 0;

    if (g.globalSwing > 0.0f && (gridPos16th % 2) != 0)
        offset += (int)(g.globalSwing * 0.5 * sixteenthNoteSamples);

    if (g.clockAlign == ClockAlign::Soft && g.clockSoftAmount > 0.0f)
        offset += (int)(randUnit01(t.rngState) * g.clockSoftAmount * 0.5 * sixteenthNoteSamples);

    return offset;
}

//==============================================================================
// 7. MODULATION OUTPUT (MIDI CC)
//
// This plugin only ever sends notes on its own; it has no idea what's
// downstream. Rather than hardcode a "meaningful" CC (mod wheel, cutoff...),
// each mod slot picks a CC *number* freely (0-127) and a source pulled from
// values already live in the track -- the number is what makes it assignable
// to literally any receiving synth's own MIDI-learn, hardware or virtual.
//==============================================================================
enum class ModSource : int
{
    Off = 0,
    Velocity,
    Gate,
    Probability,
    SequenceAmount, // the Density/Chaos knob, whichever one is active
    HarmonyDrift,
    Random,         // fresh random draw per hit -- a wandering modulation source
    Count
};

inline int getModSourceCount() { return (int)ModSource::Count; }

// A CC number to send and where its value comes from. `ccNumber` is a plain
// 0-127 int, not tied to any named CC -- callers are expected to cast it to
// EControlChangeMsg (iPlug2's IMidiMsg CC enum is just int-backed with values
// equal to the real CC numbers, so an arbitrary cast is well-defined).
struct ModSlot
{
    ModSource source = ModSource::Off;
    int ccNumber      = 1; // default CC1 (mod wheel) -- universally recognized, harmless if unmapped
};

// Value in [0, 1] for a mod slot's source, given the track's current state.
// Caller should skip sending entirely when source == Off.
inline float evaluateModSource(EuclideanTrack& t, ModSource source)
{
    switch (source)
    {
        case ModSource::Velocity:       return t.velocity;
        case ModSource::Gate:           return t.gate;
        case ModSource::Probability:    return t.probability;
        case ModSource::SequenceAmount: return t.seqMode == SequenceMode::Chaos ? t.chaosAmount : t.density;
        case ModSource::HarmonyDrift:   return t.driftAmount;
        case ModSource::Random:         return randUnit01(t.rngState);
        default:                        return 0.0f;
    }
}
