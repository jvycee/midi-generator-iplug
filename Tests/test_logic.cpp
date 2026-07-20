// Regression tests for DriftLogic.h / MidiExport.h -- the parts of
// this plugin documented as "no iPlug2 dependency, testable standalone."
// Deliberately dependency-free: no CMake, no test framework, just clang++
// and a handful of checks. Run via ./run_tests.sh.
//
// Uses explicit checks rather than assert() so failures are still caught in
// a Release-style build (assert() is a no-op under NDEBUG).

#include "../DriftLogic.h"
#include "../MidiExport.h"
#include <cstdio>

static int gFailures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        ++gFailures; \
    } } while (0)

// For float results: e.g. 0.6f + 0.3f isn't guaranteed to equal the literal
// 0.9f exactly in binary floating point, so a strict CHECK(a == b) on a
// computed float is a latent flaky-test risk.
#define CHECK_NEAR(a, b, eps) CHECK(std::fabs((a) - (b)) < (eps))

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

static void TestChordPriority()
{
    // Dominant7th (0,4,7,10) rooted at C4 (60), in C Ionian (key=0). The b7
    // (70, Bb) is not diatonic to C major -- Scale Priority must bend it
    // onto the scale, Chord Priority must leave it exactly alone.
    NoteSet scalePriority = buildChordInKey(60, ChordType::Dominant7th, VoicingStyle::Close, 4, 0, ScaleMode::Ionian, true);
    NoteSet chordPriority = buildChordInKey(60, ChordType::Dominant7th, VoicingStyle::Close, 4, 0, ScaleMode::Ionian, false);

    bool scalePriorityHasBb = std::find(scalePriority.begin(), scalePriority.end(), 70) != scalePriority.end();
    bool chordPriorityHasBb = std::find(chordPriority.begin(), chordPriority.end(), 70) != chordPriority.end();

    CHECK(!scalePriorityHasBb); // b7 must have been bent onto the scale
    CHECK(chordPriorityHasBb);  // b7 must survive exactly, unquantized

    // Chord Priority still folds into MIDI range and dedupes/sorts, same
    // invariants as always -- it only skips the scale-snapping step.
    CHECK(chordPriority.count == 4);
    for (int i = 1; i < chordPriority.count; ++i)
        CHECK(chordPriority.notes[i] > chordPriority.notes[i - 1]);
}

static void TestMonoMode()
{
    EuclideanTrack t;
    t.monoMode = true;
    NoteSet mono = buildTrackNotes(t, 67, 0, ScaleMode::Ionian);
    CHECK(mono.count == 1);
    CHECK(mono.notes[0] == 67);
}

static void TestArpCursorAdvance()
{
    // Up: cyclical ascending index sequence for a 3-note chord.
    EuclideanTrack up;
    up.arpMode = ArpMode::Up;
    int upSeq[7];
    for (int i = 0; i < 7; ++i) { upSeq[i] = up.arpIndex; advanceArpCursor(up, 3); }
    int upExpected[7] = { 0, 1, 2, 0, 1, 2, 0 };
    for (int i = 0; i < 7; ++i) CHECK(upSeq[i] == upExpected[i]);

    // Down: descending, wrapping. Starts at the default index (0) for one
    // hit before settling into the descending cycle -- see the comment on
    // advanceArpCursor; this is expected, not a bug.
    EuclideanTrack down;
    down.arpMode = ArpMode::Down;
    int downSeq[7];
    for (int i = 0; i < 7; ++i) { downSeq[i] = down.arpIndex; advanceArpCursor(down, 3); }
    int downExpected[7] = { 0, 2, 1, 0, 2, 1, 0 };
    for (int i = 0; i < 7; ++i) CHECK(downSeq[i] == downExpected[i]);

    // Up-Down: ping-pongs across [0, chordSize-1] without repeating an
    // endpoint twice in a row, for a 4-note chord.
    EuclideanTrack updown;
    updown.arpMode = ArpMode::UpDown;
    int udSeq[13];
    for (int i = 0; i < 13; ++i) { udSeq[i] = updown.arpIndex; advanceArpCursor(updown, 4); }
    int udExpected[13] = { 0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 2, 1, 0 };
    for (int i = 0; i < 13; ++i) CHECK(udSeq[i] == udExpected[i]);

    // chordSize<=1: always index 0, never advances into anything invalid.
    EuclideanTrack single;
    single.arpMode = ArpMode::Up;
    single.arpIndex = 5; // deliberately out of range for a 1-note chord
    advanceArpCursor(single, 1);
    CHECK(single.arpIndex == 0);
}

static void TestArpBuildTrackNotes()
{
    // C major triad (root=60, Major -> intervals {0,4,7}), C Ionian so
    // quantization is a no-op -> chord = {60, 64, 67}, 3 notes.
    EuclideanTrack t;
    t.chordType = ChordType::Major;
    t.chordNotes = 3;
    t.arpMode = ArpMode::Up;

    int played[6];
    for (int i = 0; i < 6; ++i)
    {
        NoteSet n = buildTrackNotes(t, 60, 0, ScaleMode::Ionian);
        CHECK(n.count == 1); // arp always resolves to exactly one note per hit
        played[i] = n.notes[0];
    }
    int expected[6] = { 60, 64, 67, 60, 64, 67 };
    for (int i = 0; i < 6; ++i) CHECK(played[i] == expected[i]);

    // Off (default): unchanged behavior -- the whole chord fires at once.
    EuclideanTrack chordMode;
    chordMode.chordType = ChordType::Major;
    chordMode.chordNotes = 3;
    NoteSet whole = buildTrackNotes(chordMode, 60, 0, ScaleMode::Ionian);
    CHECK(whole.count == 3);

    // monoMode takes priority over arpMode -- a 1-note "chord" has nothing
    // to arp through.
    EuclideanTrack mono;
    mono.monoMode = true;
    mono.arpMode = ArpMode::Up;
    NoteSet monoNote = buildTrackNotes(mono, 60, 0, ScaleMode::Ionian);
    CHECK(monoNote.count == 1);
    CHECK(monoNote.notes[0] == 60);
}

static void TestHarmonyDriftGravity()
{
    // Full gravity (1.0) must be deterministic: always the shorter path
    // toward tonic, regardless of RNG draws.
    EuclideanTrack down;
    down.driftAmount = 1.0f;
    down.driftGravity = 1.0f;
    down.scaleDegree = 2; // shorter path to 0 is down (-1)
    applyHarmonyDrift(down);
    CHECK(down.scaleDegree == 1);

    EuclideanTrack up;
    up.driftAmount = 1.0f;
    up.driftGravity = 1.0f;
    up.scaleDegree = 5; // shorter path to 0 is up, wrapping (5 -> 6 -> 0)
    applyHarmonyDrift(up);
    CHECK(up.scaleDegree == 6);

    // Gravity off (default, 0.0) must still land on a valid, in-range degree
    // -- this is the pure-random-walk path, unchanged from before gravity existed.
    EuclideanTrack none;
    none.driftAmount = 1.0f;
    applyHarmonyDrift(none);
    CHECK(none.scaleDegree >= 0 && none.scaleDegree <= 6);
}

static void TestAccent()
{
    // Off by default (amount<=0): must reproduce the input velocity exactly
    // (no arithmetic happens on this path, so strict equality is fine here).
    CHECK(applyAccent(0.6f, 0, 4, 0.0f) == 0.6f);
    CHECK(applyAccent(0.6f, 4, 4, 0.0f) == 0.6f);

    // On an accented step (stepIndex % accentEvery == 0), boosted and capped at 1.0.
    CHECK_NEAR(applyAccent(0.6f, 0, 4, 0.3f), 0.9f, 1e-5f);
    CHECK(applyAccent(0.9f, 8, 4, 0.5f) == 1.0f); // capped, not 1.4 -- exact via std::min

    // Off-beat (not a multiple of accentEvery): unaffected even with amount > 0.
    CHECK(applyAccent(0.6f, 1, 4, 0.3f) == 0.6f);
    CHECK(applyAccent(0.6f, 2, 4, 0.3f) == 0.6f);
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

static void TestAllScaleIntervalTables()
{
    // Every entry (the 7 diatonic modes plus the added exotic scales) must
    // be a well-formed 7-note scale: starts on the tonic, strictly
    // ascending, and every interval within a single octave. Catches a typo
    // in any row, not just the newly added ones.
    for (int i = 0; i < getScaleModeCount(); ++i)
    {
        const auto& intervals = getScaleIntervals((ScaleMode)i);
        CHECK(intervals[0] == 0);
        for (int j = 1; j < 7; ++j)
        {
            CHECK(intervals[j] > intervals[j - 1]);
            CHECK(intervals[j] >= 0 && intervals[j] <= 11);
        }
    }
}

static void TestCatchupFirstStep()
{
    const int cap = 8;

    // First step after transport start/reset (lastStep < 0): current only.
    CHECK(computeCatchupFirstStep(-1, 8, cap) == 8);

    // Same 16th as last (no advance): the loop [result, current] is a single
    // step; ProcessBlock's outer guard already skips the unchanged case, but
    // the helper should still be well-defined (gap 0 -> current only).
    CHECK(computeCatchupFirstStep(8, 8, cap) == 8);

    // Normal forward advance of 1: no catch-up, just the new step.
    CHECK(computeCatchupFirstStep(5, 6, cap) == 6);

    // Forward straddle of several 16ths: catch up from just after last.
    CHECK(computeCatchupFirstStep(5, 8, cap) == 6);

    // Exactly at the cap: still caught up.
    CHECK(computeCatchupFirstStep(5, 5 + cap, cap) == 6);

    // One past the cap (a locate, not a straddle): current only.
    CHECK(computeCatchupFirstStep(5, 6 + cap, cap) == 6 + cap);

    // Backward jump (loop wrap / rewind): current only, never a negative range.
    CHECK(computeCatchupFirstStep(8, 2, cap) == 2);
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

// Minimal parser for the format-0 SMF renderPatternToSMF emits: pulls out
// (absoluteTick, isNoteOn, pitch) for every note event. The exporter always
// writes explicit status bytes (no running status), so this stays simple.
// Lets tests assert structural properties a raw byte-size check can't.
struct ParsedNote { uint32_t tick; bool on; int pitch; };

static std::vector<ParsedNote> parseNoteEvents(const std::vector<uint8_t>& smf)
{
    std::vector<ParsedNote> out;
    auto readVarLen = [&](size_t& p) -> uint32_t {
        uint32_t v = 0;
        while (p < smf.size()) {
            uint8_t b = smf[p++];
            v = (v << 7) | (b & 0x7F);
            if (!(b & 0x80)) break;
        }
        return v;
    };
    // Skip MThd (14 bytes) + MTrk header (8 bytes); track data starts at 22.
    size_t i = 22;
    uint32_t tick = 0;
    while (i < smf.size()) {
        tick += readVarLen(i);
        if (i >= smf.size()) break;
        uint8_t status = smf[i++];
        if (status == 0xFF) {                 // meta
            if (i >= smf.size()) break;
            uint8_t type = smf[i++];
            uint32_t len = readVarLen(i);
            if (type == 0x2F) break;           // End of Track
            i += len;
        } else if ((status & 0xF0) == 0x90) {  // note-on
            uint8_t note = smf[i++]; uint8_t vel = smf[i++];
            out.push_back({ tick, vel > 0, (int)note });
        } else if ((status & 0xF0) == 0x80) {  // note-off
            uint8_t note = smf[i++]; i++;      // skip velocity
            out.push_back({ tick, false, (int)note });
        } else if ((status & 0xF0) == 0xB0) {  // CC
            i += 2;
        } else {
            break;                             // unexpected -- bail rather than misparse
        }
    }
    return out;
}

static void TestExportNoSamePitchOverlap()
{
    GlobalParams gp; // defaults: no swing, hard clock -> onTick lands exactly on the grid
    ModSlot slots[1];

    // Mono mode with drift off makes every hit the exact same pitch, and the
    // default 32-step note length far outlasts the 1-step spacing -- so
    // WITHOUT the same-pitch truncation this stacks 16 overlapping note-ons of
    // one pitch. Every step fires (pulses == steps).
    EuclideanTrack track;
    track.steps = 4;
    track.pulses = 4;
    track.monoMode = true;
    track.driftAmount = 0.0f;
    rebuildTrackPattern(track);

    auto smf = MidiExport::renderPatternToSMF(track, gp, 120.0, 1, slots, 0, 8);
    auto notes = parseNoteEvents(smf);
    CHECK(!notes.empty());

    // Walk events in tick/priority order (already sorted by the exporter, with
    // note-off before note-on at equal tick). A note-on for a pitch already
    // sounding is an illegal overlap.
    bool sounding[128] = {};
    bool overlap = false;
    int noteOnCount = 0;
    for (const auto& e : notes) {
        if (e.on) {
            if (sounding[e.pitch]) overlap = true;
            sounding[e.pitch] = true;
            ++noteOnCount;
        } else {
            sounding[e.pitch] = false;
        }
    }
    CHECK(!overlap);
    CHECK(noteOnCount == 16); // 1 bar * 16 steps, every step fires, mono
}

// Confirms MidiExport.h needs zero Arp-specific code: renderPatternToSMF takes
// its EuclideanTrack by value and calls the same buildTrackNotes as
// ProcessStep, so setting track.arpMode before export should be enough to
// make the export inherit arp behavior with no changes to MidiExport.h itself.
static void TestExportArpMode()
{
    GlobalParams gp; // C Ionian, so the C major triad below is unaffected by quantization
    ModSlot slots[1];

    EuclideanTrack track;
    track.steps = 4;
    track.pulses = 4; // dense pattern -> every 16th in the bar fires (16 hits/bar)
    track.chordType = ChordType::Major; // root=60 -> triad {60, 64, 67}
    track.arpMode = ArpMode::Up;
    rebuildTrackPattern(track);

    auto smf = MidiExport::renderPatternToSMF(track, gp, 120.0, 1, slots, 0, 8);
    auto notes = parseNoteEvents(smf);

    std::vector<int> onPitches;
    for (const auto& e : notes)
        if (e.on) onPitches.push_back(e.pitch);

    // Arp resolves every hit to exactly one note, cycling Up through the
    // triad -- not the whole chord firing at once (which would be 3 note-ons
    // per hit, 48 total, not cycling).
    CHECK(onPitches.size() == 16);
    int expected[16] = { 60,64,67,60, 64,67,60,64, 67,60,64,67, 60,64,67,60 };
    for (size_t i = 0; i < onPitches.size() && i < 16; ++i)
        CHECK(onPitches[i] == expected[i]);

    // Sanity check against the same track with Arp off: the whole chord
    // should fire at once, 3x the note-on count.
    EuclideanTrack chordTrack = track;
    chordTrack.arpMode = ArpMode::Off;
    rebuildTrackPattern(chordTrack);
    auto smfChord = MidiExport::renderPatternToSMF(chordTrack, gp, 120.0, 1, slots, 0, 8);
    auto notesChord = parseNoteEvents(smfChord);
    int onCountChord = 0;
    for (const auto& e : notesChord) if (e.on) ++onCountChord;
    CHECK(onCountChord == 48);
}

// Underlying invariant kParamDeterministicExport (Drift.cpp) depends on:
// renderPatternToSMF is a pure function of its EuclideanTrack argument, with
// no external/hidden state (wall clock, global RNG, etc.) leaking in. Uses
// Chaos mode specifically -- it's the export path most likely to have picked
// up an accidental external dependency, since it evolves a floating-point
// logistic-map value every hit.
static void TestExportDeterministicGivenSameState()
{
    GlobalParams gp;
    ModSlot slots[1];

    EuclideanTrack track;
    track.steps = 16;
    track.pulses = 11;
    track.seqMode = SequenceMode::Chaos;
    track.chaosAmount = 0.8f;
    track.chaosX = 0.37; // fixed starting point, standing in for ReseedTrackDeterministic's seeding
    track.rngState = 0x44726674u;
    track.chordType = ChordType::Minor7th;
    track.arpMode = ArpMode::UpDown;
    rebuildTrackPattern(track);

    EuclideanTrack trackCopy = track; // identical starting state, separate struct

    auto smfA = MidiExport::renderPatternToSMF(track, gp, 120.0, 2, slots, 0, 8);
    auto smfB = MidiExport::renderPatternToSMF(trackCopy, gp, 120.0, 2, slots, 0, 8);

    CHECK(smfA.size() == smfB.size());
    CHECK(smfA == smfB);
}

static void TestExportRatchetOverlapClean()
{
    GlobalParams gp; // no swing, hard clock
    ModSlot slots[1];

    // Worst-case ratchet overlap: every 16th fires (steps=pulses=1), mono, a
    // long note so the ratchet span caps at a full beat (4 steps), ratchet 8.
    // Each burst's slices reach 3 steps past its own step, so at any moment
    // several bursts' queued slices coexist -- exactly the shape that used to
    // produce (a) off-before-on orphans when a new burst cancelled a future
    // slice by truncation, and (b) bogus voice-stealing when queued slices
    // were counted as simultaneous voices.
    EuclideanTrack track;
    track.steps = 1;
    track.pulses = 1;
    track.monoMode = true;
    track.driftAmount = 0.0f;
    track.noteLengthSteps = 64;
    track.ratchetCount = 8;
    rebuildTrackPattern(track);

    auto smf = MidiExport::renderPatternToSMF(track, gp, 120.0, 2, slots, 0, 8);
    auto notes = parseNoteEvents(smf);
    CHECK(!notes.empty());

    // Walking in file order (offs sort before ons at equal ticks), the single
    // pitch must strictly alternate: an on while already sounding is an
    // overlap; an off while silent is an orphan (its on was cancelled but the
    // off survived, or vice versa).
    bool sounding[128] = {};
    bool overlap = false, orphanOff = false;
    int ons = 0;
    for (const auto& e : notes) {
        if (e.on) {
            if (sounding[e.pitch]) overlap = true;
            sounding[e.pitch] = true;
            ++ons;
        } else {
            if (!sounding[e.pitch]) orphanOff = true;
            sounding[e.pitch] = false;
        }
    }
    CHECK(!overlap);
    CHECK(!orphanOff);
    CHECK(ons > 0);
}

int main()
{
    TestEuclideanPatterns();
    TestTrigCondition();
    TestRotationDrift();
    TestPatternInvert();
    TestChordBuilding();
    TestChordPriority();
    TestMonoMode();
    TestArpCursorAdvance();
    TestArpBuildTrackNotes();
    TestHarmonyDriftGravity();
    TestAccent();
    TestScaleQuantization();
    TestAllScaleIntervalTables();
    TestCatchupFirstStep();
    TestMidiExportProducesAFile();
    TestRatchet();
    TestExportNoSamePitchOverlap();
    TestExportArpMode();
    TestExportDeterministicGivenSameState();
    TestExportRatchetOverlapClean();

    if (gFailures == 0)
        std::printf("All tests passed.\n");
    else
        std::fprintf(stderr, "%d check(s) failed.\n", gFailures);

    return gFailures == 0 ? 0 : 1;
}
