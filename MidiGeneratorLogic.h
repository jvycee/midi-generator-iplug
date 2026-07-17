#pragma once

#include <vector>
#include <array>
#include <algorithm>   // FIX: std::sort / std::unique were used without this.
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
//==============================================================================
inline std::vector<bool> computeEuclidean(int pulses, int steps)
{
    if (steps <= 0) return {};
    if (pulses <= 0) return std::vector<bool>(steps, false);
    if (pulses >= steps) return std::vector<bool>(steps, true);

    std::vector<bool> result(steps, false);

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
            result[i] = true;
        }
    }
    return result;
}

// Positive rotation shifts hits *earlier* (pattern moves left). If you'd rather
// the knob push hits later, negate `rotation` at the call site in
// rebuildTrackPattern rather than here, so saved presets keep their meaning.
inline std::vector<bool> rotatePattern(std::vector<bool> p, int rotation)
{
    int n = (int)p.size();
    if (n <= 1) return p;
    rotation = ((rotation % n) + n) % n;
    if (rotation > 0) 
        std::rotate(p.begin(), p.begin() + rotation, p.end());
    return p;
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

inline const std::vector<int>& getChordIntervals(ChordType type)
{
    static const std::array<std::vector<int>, (size_t)ChordType::Count> table =
    {{
        {0},
        {0, 7},
        {0, 12},
        {0, 4, 7},
        {0, 3, 7},
        {0, 3, 6},
        {0, 4, 8},
        {0, 2, 7},
        {0, 5, 7},
        {0, 4, 7, 11},
        {0, 3, 7, 10},
        {0, 4, 7, 10},
        {0, 3, 7, 10, 14},
        {0, 4, 7, 11, 14},
        {0, 4, 7, 10, 14},
        {0, 3, 7, 10, 14, 17},
        {0, 4, 7, 9},
        {0, 3, 7, 9},
        {0, 3, 6, 10},
        {0, 3, 6, 9},
    }};
    return table[(size_t)type];
}

inline const char* getChordName(ChordType type)
{
    static const char* names[] = {
        "Single", "Power",  "Octave",  "Major",  "Minor",
        "Dim",    "Aug",    "Sus2",    "Sus4",   "Maj7",
        "Min7",   "Dom7",   "Min9",    "Maj9",   "Dom9",
        "Min11",  "Maj6",   "Min6",    "HalfDim7","Dim7"
    };
    return names[(int)type];
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

inline const char* getVoicingName(VoicingStyle v)
{
    static const char* names[] = {"Close", "Drop2", "Drop3", "Spread", "Wide"};
    return names[(int)v];
}

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

inline std::vector<int> buildChord(int rootNote, ChordType type,
                                    VoicingStyle voicing, int numNotes,
                                    int spreadOctaves)
{
    const auto& intervals = getChordIntervals(type);
    if (intervals.empty()) return {rootNote};

    std::vector<int> raw;
    raw.reserve((size_t)std::max(numNotes, (int)intervals.size()));
    for (int interval : intervals)
        raw.push_back(rootNote + interval);

    int rawCount = (int)raw.size();

    if (numNotes <= 1)
        return {raw[0]};

    if (numNotes <= rawCount)
        raw.resize((size_t)numNotes);
    else
        while (raw.size() < (size_t)numNotes)
            raw.push_back(raw[raw.size() - (size_t)rawCount] + 12);

    std::vector<int> notes;
    switch (voicing)
    {
    case VoicingStyle::Close:
        notes = raw;
        break;

    case VoicingStyle::Drop2:
        notes = raw;
        if ((int)notes.size() >= 4) notes[notes.size() - 2] -= 12;
        break;

    case VoicingStyle::Drop3:
        notes = raw;
        if ((int)notes.size() >= 4) notes[notes.size() - 3] -= 12;
        break;

    case VoicingStyle::Spread:
        // Fans voices outward alternately. Deliberately extreme: with 6 voices
        // this spans ~6 octaves, so the fold above does real work here.
        notes = raw;
        for (int i = 0; i < (int)notes.size(); ++i)
        {
            if (i % 2 == 1) notes[i] += 12 * ((i / 2) + 1);
            else if (i > 0) notes[i] -= 12 * (i / 2);
        }
        break;

    case VoicingStyle::Wide:
        notes = raw;
        for (int i = 1; i < (int)notes.size(); ++i)
            notes[i] += 12 * spreadOctaves * i; // FIX: actually spread them by octaves based on index
        break;

    default:
        notes = raw;
        break;
    }

    // FIX: Only add full octaves to preserve chord harmony.
    // Integer division is applied to the octave multiplier, not the semitones.
    if (spreadOctaves > 0 && voicing != VoicingStyle::Wide)
        for (int i = 1; i < (int)notes.size(); ++i)
            notes[i] += 12 * ((spreadOctaves * i) / (int)notes.size());

    for (int& n : notes)
        n = foldIntoMidiRange(n);

    std::sort(notes.begin(), notes.end());

    // FIX: collapse duplicates. Two note-ons for the same pitch on the same
    // channel means the first note-off releases both -- and the second is
    // orphaned. (A MIDI chord is a set of pitches; sorting here is harmless.)
    notes.erase(std::unique(notes.begin(), notes.end()), notes.end());

    return notes;
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

inline const char* getScaleModeName(ScaleMode m)
{
    static const char* names[] = {
        "Ionian", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Aeolian", "Locrian"
    };
    return names[(int)m];
}

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

inline const char* getKeyName(int pitchClass)
{
    static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    return names[((pitchClass % 12) + 12) % 12];
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
inline std::vector<int> buildChordInKey(int rootNote, ChordType type, VoicingStyle voicing,
                                         int numNotes, int spreadOctaves,
                                         int key, ScaleMode mode)
{
    auto notes = buildChord(rootNote, type, voicing, numNotes, spreadOctaves);

    for (int& n : notes)
        n = foldIntoMidiRange(quantizeToScale(n, key, mode));

    std::sort(notes.begin(), notes.end());
    notes.erase(std::unique(notes.begin(), notes.end()), notes.end());

    if (!notes.empty() && (int)notes.size() < numNotes)
    {
        // Index against the *current* (growing) size, not the original --
        // otherwise when the chord collapsed to a single tone, every pass
        // retries the exact same +12 doubling forever instead of stacking
        // successive octaves (+12, +24, +36, ...) to actually reach numNotes.
        size_t tries = 0;
        while ((int)notes.size() < numNotes && tries < (size_t)numNotes * 8)
        {
            int doubled = foldIntoMidiRange(notes[tries % notes.size()] + 12);
            if (std::find(notes.begin(), notes.end(), doubled) == notes.end())
                notes.push_back(doubled);
            ++tries;
        }
        std::sort(notes.begin(), notes.end());
    }

    return notes;
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

inline const char* getSequenceModeName(SequenceMode m)
{
    static const char* names[] = { "Euclidean", "Density", "Chaos" };
    return names[(int)m];
}

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

    ChordType chordType     = ChordType::SingleNote;
    VoicingStyle voicing    = VoicingStyle::Close;
    int chordNotes          = 3;
    int chordSpread         = 0;

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
    bool solo       = false;   // declared but never read anywhere.

    std::vector<bool> pattern;
    int currentStep = -1;
};

// Notes for one triggered hit. Mono mode bypasses the chord engine entirely
// and returns just the scale-quantized root -- see EuclideanTrack::monoMode.
inline std::vector<int> buildTrackNotes(const EuclideanTrack& t, int scaleRoot, int key, ScaleMode mode)
{
    if (t.monoMode)
        return { foldIntoMidiRange(scaleRoot) };

    return buildChordInKey(scaleRoot, t.chordType, t.voicing, t.chordNotes, t.chordSpread, key, mode);
}

//==============================================================================
// 5. GLOBAL CONFIG
//==============================================================================
constexpr int kMaxTracks = 4;

enum class ClockAlign : int
{
    Hard = 0,   // note-ons land exactly on the grid
    Soft = 1    // note-ons get a small, per-hit randomized late nudge off the grid
};

inline const char* getClockAlignName(ClockAlign a)
{
    static const char* names[] = { "Hard", "Soft" };
    return names[(int)a];
}

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
    t.pattern = computeEuclidean(t.pulses, t.steps);
    t.pattern = rotatePattern(t.pattern, t.rotation);
}

inline void rebuildAllPatterns(std::vector<EuclideanTrack>& tracks)
{
    for (auto& t : tracks)
        rebuildTrackPattern(t);
}

// Decides whether `stepIndex` fires, for whichever SequenceMode the track is
// in, then thins the result by `probability`. This is the one place that
// reads `pattern` for Euclidean mode and the only place `probability` (and
// the per-track RNG) gets used -- previously declared on the track but never
// consulted.
inline bool evaluateStepTrigger(EuclideanTrack& t, int stepIndex)
{
    bool hit = false;

    switch (t.seqMode)
    {
        case SequenceMode::Euclidean:
            hit = !t.pattern.empty() && t.pattern[stepIndex % (int)t.pattern.size()];
            break;

        case SequenceMode::Density:
            hit = randUnit01(t.rngState) < t.density;
            break;

        case SequenceMode::Chaos:
        {
            double amt = t.chaosAmount < 0.f ? 0.f : (t.chaosAmount > 1.f ? 1.f : t.chaosAmount);
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

inline const char* getModSourceName(ModSource s)
{
    static const char* names[] = {
        "Off", "Velocity", "Gate", "Probability", "Dens/Chaos", "Drift", "Random"
    };
    return names[(int)s];
}

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