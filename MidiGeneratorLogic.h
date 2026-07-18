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
    Count
};

inline int getScaleModeCount() { return (int)ScaleMode::Count; }

inline const std::array<int, 7>& getScaleIntervals(ScaleMode m)
{
    static const std::array<std::array<int, 7>, 7> table = {{
        {0, 2, 4, 5, 7, 9, 11},  // Ionian
        {0, 2, 3, 5, 7, 9, 10},  // Dorian
        {0, 1, 3, 5, 7, 8, 10},  // Phrygian
        {0, 2, 4, 6, 7, 9, 11},  // Lydian
        {0, 2, 4, 5, 7, 9, 10},  // Mixolydian
        {0, 2, 3, 5, 7, 8, 10},  // Aeolian
        {0, 1, 3, 5, 6, 8, 10},  // Locrian
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

// Builds a chord as buildChord() does, then pulls every tone back onto the
// chosen key/scale. Re-sorts and re-dedupes afterward: independent
// quantization can collapse two chord tones onto the same pitch (e.g. a
// diminished 5th snapping to the same scale step as its neighbor), and two
// note-ons for the same pitch is the duplicate-note-off hazard buildChord()
// already guards against above.
//
// That collapse used to just silently return fewer notes than `numNotes` --
// which is exactly why some hits come out noticeably quieter than others
// with everything else held constant (a 2-note dyad sums to a lot less
// signal than the 4-voice chord the next hit gets). To keep loudness
// consistent hit-to-hit, pad back up to `numNotes` by octave-doubling
// existing tones rather than leaving the chord thinned.
inline NoteSet buildChordInKey(int rootNote, ChordType type, VoicingStyle voicing,
                                int numNotes, int key, ScaleMode mode)
{
    NoteSet result = buildChord(rootNote, type, voicing, numNotes);

    for (int& n : result)
        n = foldIntoMidiRange(quantizeToScale(n, key, mode));

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

    // When on, every hit is exactly the scale-quantized root note -- no
    // chord/voicing math at all. For lead lines, bass, and monophonic synth
    // patches, which want one note per hit and no ambiguity about which
    // chord tone gets sent to a synth that can only play one note anyway.
    bool monoMode = false;

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

    // Retriggers a hit ratchetCount times within the current 16th-note step
    // instead of one sustained note-on. 1 = off (unchanged behavior). Scoped
    // to hits that resolve to a single note (monoMode, or any chord voicing
    // that happens to collapse to one note) -- ratcheting a full chord would
    // multiply note count by ratchetCount per hit, straight into the
    // polyphony cap; a rapid single-note burst is the actually-useful case
    // (drum-machine-style rolls) and never stresses voice count regardless
    // of ratchetCount, since retriggers are sequential, not simultaneous.
    // Overrides noteLengthSteps/gate for that hit -- a ratchet is a burst
    // confined to one step, not N copies of a potentially many-bar sustain.
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

    // Heads-up: every track defaults to channel 1. Two tracks whose chords
    // overlap on a pitch will steal each other's note-offs. Consider
    // defaulting track N to channel N.
    int midiChannel = 1;
    int rateDiv     = 16;
    bool muted      = false;

    uint32_t patternBits = 0; // bit i set => step i fires (Euclidean mode only)
    int currentStep = -1;
};

// Notes for one triggered hit. Mono mode bypasses the chord engine entirely
// and returns just the scale-quantized root -- see EuclideanTrack::monoMode.
inline NoteSet buildTrackNotes(const EuclideanTrack& t, int scaleRoot, int key, ScaleMode mode)
{
    if (t.monoMode)
    {
        NoteSet single;
        single.push(foldIntoMidiRange(scaleRoot));
        return single;
    }

    return buildChordInKey(scaleRoot, t.chordType, t.voicing, t.chordNotes, key, mode);
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
    t.patternBits = computeEuclidean(t.pulses, t.steps);
    t.patternBits = rotatePattern(t.patternBits, t.steps, t.rotation);

    if (t.patternInvert)
    {
        int steps = std::clamp(t.steps, 0, 32);
        uint32_t mask = (steps >= 32) ? 0xFFFFFFFFu : ((1u << steps) - 1u);
        t.patternBits = (~t.patternBits) & mask;
    }
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
            uint32_t bits = rotationDriftSteps != 0 ? rotatePattern(t.patternBits, steps, rotationDriftSteps) : t.patternBits;
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
inline void applyHarmonyDrift(EuclideanTrack& t)
{
    if (t.driftAmount <= 0.0f) return;

    if (randUnit01(t.rngState) < t.driftAmount)
    {
        int step = (randUnit01(t.rngState) < 0.5f) ? -1 : 1;
        t.scaleDegree = ((t.scaleDegree + step) % 7 + 7) % 7;
    }
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
