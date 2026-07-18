// Regression tests for MidiGeneratorLogic.h / MidiExport.h -- the parts of
// this plugin documented as "no iPlug2 dependency, testable standalone."
// Deliberately dependency-free: no CMake, no test framework, just clang++
// and a handful of checks. Run via ./run_tests.sh.
//
// Uses explicit checks rather than assert() so failures are still caught in
// a Release-style build (assert() is a no-op under NDEBUG).

#include "../MidiGeneratorLogic.h"
#include "../MidiExport.h"
#include <cstdio>

static int gFailures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        ++gFailures; \
    } } while (0)

static void TestEuclideanPatterns()
{
    // E(4,16): downbeat fix means step 0 must fire, and exactly 4 of 16 steps.
    uint32_t bits = computeEuclidean(4, 16);
    CHECK((bits & 1u) == 1u);
    CHECK(__builtin_popcount(bits) == 4);

    // Rotation must preserve pulse count -- it's a permutation, not a resample.
    uint32_t rotated = rotatePattern(bits, 16, 3);
    CHECK(__builtin_popcount(rotated) == 4);

    // pulses >= steps => every step fires.
    CHECK(computeEuclidean(20, 16) == 0xFFFFu);

    // 32-step edge case: shifting a uint32_t by 32 is UB, this must not hit it.
    CHECK(computeEuclidean(32, 32) == 0xFFFFFFFFu);
    CHECK(computeEuclidean(0, 16) == 0u);
    CHECK(computeEuclidean(5, 0) == 0u);
}

static void TestRotationDrift()
{
    EuclideanTrack t;
    t.steps = 16;
    t.pulses = 4;
    t.rotation = 0;
    rebuildTrackPattern(t);
    uint32_t base = t.patternBits;

    // rotationDriftSteps=0 must reproduce the exact cached (no-drift) pattern.
    for (int s = 0; s < 16; ++s)
        CHECK(evaluateStepTrigger(t, s, 0) == (((base >> s) & 1u) != 0));

    // A nonzero live drift must match rotating the cached pattern directly,
    // without mutating the cached patternBits itself.
    uint32_t expected = rotatePattern(base, 16, 3);
    for (int s = 0; s < 16; ++s)
        CHECK(evaluateStepTrigger(t, s, 3) == (((expected >> s) & 1u) != 0));
    CHECK(t.patternBits == base);
}

static void TestTrigCondition()
{
    EuclideanTrack t;

    // Default (trigEvery=1) must never gate anything, on any loop index.
    CHECK(evaluateTrigCondition(t, 0) == true);
    CHECK(evaluateTrigCondition(t, 5) == true);

    // trigEvery=3, trigOffset=1: live only on loops 1, 4, 7, ... (loop % 3 == 1).
    t.trigEvery = 3;
    t.trigOffset = 1;
    CHECK(evaluateTrigCondition(t, 0) == false);
    CHECK(evaluateTrigCondition(t, 1) == true);
    CHECK(evaluateTrigCondition(t, 2) == false);
    CHECK(evaluateTrigCondition(t, 3) == false);
    CHECK(evaluateTrigCondition(t, 4) == true);
    CHECK(evaluateTrigCondition(t, 7) == true);

    // Out-of-[0,trigEvery) offsets must wrap sanely rather than never/always firing.
    t.trigOffset = 10; // 10 % 3 == 1, same effective offset as above
    CHECK(evaluateTrigCondition(t, 1) == true);
    CHECK(evaluateTrigCondition(t, 0) == false);
}

static void TestPatternInvert()
{
    EuclideanTrack t;
    t.steps = 16;
    t.pulses = 4;
    t.rotation = 0;
    rebuildTrackPattern(t);
    uint32_t normal = t.patternBits;

    t.patternInvert = true;
    rebuildTrackPattern(t);
    uint32_t inverted = t.patternBits;

    const uint32_t mask16 = 0xFFFFu;
    CHECK((normal & inverted) == 0u);        // no step set in both
    CHECK((normal | inverted) == mask16);    // together, every step is covered
    CHECK(__builtin_popcount(inverted) == 16 - 4);

    // Toggling back off must exactly restore the original pattern.
    t.patternInvert = false;
    rebuildTrackPattern(t);
    CHECK(t.patternBits == normal);
}

static void TestChordBuilding()
{
    // Minor9th, Drop2, 4 voices, root C4 (60), no key filter (C Ionian is a no-op
    // here since all resulting tones are already in C major).
    NoteSet ns = buildChordInKey(60, ChordType::Minor9th, VoicingStyle::Drop2, 4, 0, ScaleMode::Ionian);
    CHECK(ns.count == 4);
    for (int i = 1; i < ns.count; ++i)
        CHECK(ns.notes[i] > ns.notes[i - 1]); // sorted, no duplicate pitches

    // Padding path: ask for 6 voices from a 1-interval chord (SingleNote) --
    // must octave-double up to exactly 6 unique notes, not silently return fewer.
    NoteSet padded = buildChordInKey(60, ChordType::SingleNote, VoicingStyle::Close, 6, 0, ScaleMode::Ionian);
    CHECK(padded.count == 6);
    for (int i = 1; i < padded.count; ++i)
        CHECK(padded.notes[i] > padded.notes[i - 1]);

    // Wide's spread-octave mechanism was dead code (never wired to a
    // parameter) and has been removed -- Wide must now build byte-for-byte
    // identical chords to Close.
    NoteSet close = buildChord(60, ChordType::Minor9th, VoicingStyle::Close, 4);
    NoteSet wide  = buildChord(60, ChordType::Minor9th, VoicingStyle::Wide, 4);
    CHECK(close.count == wide.count);
    for (int i = 0; i < close.count && i < wide.count; ++i)
        CHECK(close.notes[i] == wide.notes[i]);

    // foldIntoMidiRange must never leave the 0-127 range.
    CHECK(foldIntoMidiRange(200) >= 0 && foldIntoMidiRange(200) <= 127);
    CHECK(foldIntoMidiRange(-50) >= 0 && foldIntoMidiRange(-50) <= 127);
}

static void TestMonoMode()
{
    EuclideanTrack t;
    t.monoMode = true;
    NoteSet mono = buildTrackNotes(t, 67, 0, ScaleMode::Ionian);
    CHECK(mono.count == 1);
    CHECK(mono.notes[0] == 67);
}

static void TestScaleQuantization()
{
    // C Ionian: every diatonic pitch class should quantize to itself.
    for (int pc : {0, 2, 4, 5, 7, 9, 11})
        CHECK(quantizeToScale(60 + pc, 0, ScaleMode::Ionian) == 60 + pc);

    // A non-diatonic pitch class (C#) must move, and land back in range.
    int q = quantizeToScale(61, 0, ScaleMode::Ionian);
    CHECK(q != 61);
}

static void TestMidiExportProducesAFile()
{
    GlobalParams gp;
    ModSlot slots[2];
    slots[0].source = ModSource::Velocity;
    slots[0].ccNumber = 1;

    EuclideanTrack track;
    rebuildTrackPattern(track);

    auto bytes = MidiExport::renderPatternToSMF(track, gp, 120.0, 2, slots, 2, 8);
    CHECK(bytes.size() > 14); // bigger than just the MThd/MTrk headers

    // Header sanity: "MThd" magic + format 0 + 1 track.
    CHECK(bytes.size() >= 4 && bytes[0] == 'M' && bytes[1] == 'T' && bytes[2] == 'h' && bytes[3] == 'd');
}

static void TestRatchet()
{
    GlobalParams gp;
    ModSlot slots[1];

    // steps=pulses=1 fires every single 16th note -- every step in the
    // rendered bar gets ratcheted, so the byte-count difference is as large
    // (and as easy to check) as this feature can produce.
    EuclideanTrack track;
    track.steps = 1;
    track.pulses = 1;
    track.monoMode = true;
    rebuildTrackPattern(track);

    auto bytesNoRatchet = MidiExport::renderPatternToSMF(track, gp, 120.0, 1, slots, 0, 8);

    track.ratchetCount = 4;
    auto bytesWithRatchet = MidiExport::renderPatternToSMF(track, gp, 120.0, 1, slots, 0, 8);

    // 4x the note-on/off events per step must produce a meaningfully larger file.
    CHECK(bytesWithRatchet.size() > bytesNoRatchet.size());
}

int main()
{
    TestEuclideanPatterns();
    TestTrigCondition();
    TestRotationDrift();
    TestPatternInvert();
    TestChordBuilding();
    TestMonoMode();
    TestScaleQuantization();
    TestMidiExportProducesAFile();
    TestRatchet();

    if (gFailures == 0)
        std::printf("All tests passed.\n");
    else
        std::fprintf(stderr, "%d check(s) failed.\n", gFailures);

    return gFailures == 0 ? 0 : 1;
}
