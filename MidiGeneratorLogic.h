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
// 3. EUCLIDEAN TRACK
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

    float velocity      = 0.8f;
    float gate          = 0.5f;
    float probability   = 1.0f;

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

//==============================================================================
// 4. GLOBAL CONFIG
//==============================================================================
constexpr int kMaxTracks = 4;

struct GlobalParams
{
    float globalSwing     = 0.0f;
    int   globalTranspose = 0;
    bool  enabled         = true;
};

//==============================================================================
// 5. UTILITY
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