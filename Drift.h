#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "DriftLogic.h"

using namespace iplug;

enum EParams
{
  kParamPulses = 0,
  kParamSequenceMode,      // Euclidean / Density / Chaos
  kParamSequenceAmount,    // Density: hit chance. Chaos: logistic-map amount.
  kParamProbability,       // per-hit thinning, on top of whichever mode fires
  kParamSwing,
  kParamClockAlign,        // Hard / Soft
  kParamClockSoftAmount,
  kParamKey,               // tonic, C..B
  kParamScaleMode,         // Ionian/Dorian/Phrygian/Lydian/Mixolydian/Aeolian/Locrian
  kParamChordType,         // Single/Power/Octave/Major/Minor/Sus/Dim/... (see ChordType)
  kParamChordVoices,       // note count: 1 = single note, 2 = dyad, 3 = triad, 4+ = extended chord
  kParamHarmonyDrift,      // chance per hit to step the chord root by one scale degree
  kParamSteps,             // pattern length
  kParamRotation,          // pattern rotation
  kParamRootNote,          // anchor/register note; actual pitch is scale-quantized from this
  kParamVoicing,           // Close/Drop2/Drop3/Spread/Wide
  kParamNoteLength,        // note's sustain span, in 16th-note steps -- can span many bars
  kParamGate,              // fraction of that span actually held
  kParamVelocity,
  kParamMidiChannel,
  kParamGlobalTranspose,
  kParamMod1Source,        // what drives Mod slot 1's CC value
  kParamMod1CC,            // which CC number (0-127) it's sent as
  kParamMod2Source,
  kParamMod2CC,
  kParamExportBars,        // how many bars to render when dragging out to the DAW
  kParamMonoMode,          // on = every hit is a single scale-quantized note (leads/bass/mono synths)
  kParamMaxVoices,         // global polyphony cap across all overlapping hits (not just one chord's Voices)
  // Mod3-8: same idea as Mod1/2, just more of them -- for patching into a
  // modular rig via a MIDI-to-CV interface, 2 simultaneous CC/CV outs is
  // limiting (filter, VCA, pan, extra LFO target, ...).
  kParamMod3Source,
  kParamMod3CC,
  kParamMod4Source,
  kParamMod4CC,
  kParamMod5Source,
  kParamMod5CC,
  kParamMod6Source,
  kParamMod6CC,
  kParamMod7Source,
  kParamMod7CC,
  kParamMod8Source,
  kParamMod8CC,
  kParamSendClock,         // send standard 24ppqn MIDI clock (+ Start/Stop) alongside notes/CC, for external gear to sync to
  kParamUIViewMode,        // Full/Compact WebView layout choice -- UI state only, never read by ProcessBlock. Meta + non-automatable (see constructor); rides normal host state save/recall so the choice survives plugin reopen.
  kParamPatternInvert,     // flips active/inactive Euclidean steps -- appended last so no existing param index shifts
  kParamTrigEvery,         // track only plays once every N loops through the pattern (see evaluateTrigCondition)
  kParamTrigOffset,        // which of every kParamTrigEvery loops it plays on
  kParamRotationDriftPeriod, // 16th-notes between each +1 auto-advance of the live rotation offset; 0 = off
  kParamRatchetCount,      // retriggers per step for single-note hits; 1 = off
  kParamChordPriority,     // off = Scale Priority (quantize chord tones into key, default); on = Chord Priority (keep exact chromatic chord, unquantized)
  kParamDriftGravity,      // 0 = pure random walk (default); 1 = harmony drift always steps toward the tonic
  kParamAccentEvery,       // accent every Nth step
  kParamAccentAmount,      // velocity boost on accented steps; 0 = off (default)
  kParamFreeze,            // stop triggering new hits, let whatever's ringing continue -- see ProcessBlock
  kParamExportVariations,  // how many independently-reseeded takes to render on export; 1 = current single-file behavior
  kParamArpMode,           // Off/Up/Down/Up-Down -- steps through the chord one note per hit instead of firing it all at once (see ArpMode)
  kParamDeterministicExport, // off (default) = current behavior (variation 0 = live track state, others = std::random_device reseed); on = every variation reseeds from a fixed seed, so re-exporting the same settings is byte-for-byte reproducible
  kNumParams
};

// WebView UI protocol (see resources/web/app.js). Wire values only -- the two
// sides agree on these numbers, not on a shared header.
constexpr int kMsgTagExportRequest   = 100;
constexpr int kMsgTagExportSucceeded = 101;
constexpr int kMsgTagExportFailed    = 102;
constexpr int kMsgTagTempoUpdate     = 103; // C++ -> JS: live host tempo, see OnIdle()
constexpr int kMsgTagRerollRequest   = 104; // JS -> C++: reseed the generative wander, see OnMessage()

// Fixed-capacity, allocation-free stand-in for std::vector, sized for the
// note lists below. push()/removeAt() never touch the heap, which is what
// actually matters on the audio thread -- ProcessBlock adds and removes from
// these every block.
template <typename T, int N>
struct FixedList
{
    T items[N];
    int count = 0;

    bool push(const T& v) { if (count >= N) return false; items[count++] = v; return true; }
    void removeAt(int i)  { for (int j = i; j < count - 1; ++j) items[j] = items[j + 1]; --count; }
    void clear()          { count = 0; }
    bool empty() const    { return count == 0; }

    T&       front()       { return items[0]; }
    const T& front() const { return items[0]; }

    T*       begin()       { return items; }
    T*       end()         { return items + count; }
    const T* begin() const { return items; }
    const T* end()   const { return items + count; }
};

class Drift final : public Plugin
{
public:
    Drift(const InstanceInfo& info);

    // This replaces JUCE's prepareToPlay
    void OnReset() override;

    // Triggered whenever a UI knob or host automation changes
    void OnParamChange(int paramIdx) override;

    bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;

    // This replaces JUCE's processBlock
    void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;

    // UI-thread periodic tick (framework-driven, not audio-rate). Used only
    // to push live tempo to the WebView UI -- see Drift.cpp. The
    // ambient breathing animation's cycle length is tempo-relative, so
    // without this it's stuck at app.js's hardcoded 120bpm fallback.
    void OnIdle() override;

    // Renders the current pattern (kParamExportBars bars, current tempo) to
    // a temp .mid file and returns its path (empty on failure). If
    // kParamExportVariations > 1, renders that many independently-reseeded
    // takes as numbered files and returns the path to the first one. Called
    // from OnMessage() in response to the WebView UI's export button
    // (msgTag 100) -- see MidiExport.h for the actual render/write.
    WDL_String ExportPatternAsMidiFile();

private:
    // Our core generator state
    EuclideanTrack track;
    GlobalParams globalParams;

    // MIDI CC output: eight independently assignable modulation slots. Each
    // one is sent (a) immediately on every triggered hit, tightly synced to
    // the note, and (b) on its own ~20ms ticker regardless of hits, so it
    // behaves like continuous CV rather than a value that only updates when
    // something plays -- important once this is feeding a modular rig
    // through a MIDI-to-CV interface rather than just a synth's mod matrix.
    // The CC *number* is freely chosen (not tied to a named CC), so
    // whatever's downstream -- hardware or virtual -- just gets MIDI-learned
    // or patched to whichever slot number.
    ModSlot modSlots[8];
    int ccUpdateSamplesRemaining = 0;
    void SendModCCs(); // shared by the ticker and on-hit paths in ProcessBlock

    // Last value actually sent per mod slot (quantized to the 0-127 MIDI
    // byte, sidestepping float-epsilon comparisons), so the 50Hz ticker
    // skips resending a value that hasn't changed since the last tick --
    // Velocity/Gate/Probability/Drift only actually change on a new hit, so
    // between hits this was flooding whatever's downstream with identical
    // CC messages 50 times a second. -1 = "never sent", forcing the first
    // send through. ModSource::Random is exempt (see SendModCCs) since it's
    // supposed to move continuously.
    int lastSentCCValue[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };

    // Last tempo value pushed to the UI, so OnIdle only sends on an actual
    // change rather than flooding the WebView message queue every tick.
    double lastSentTempo = -1.0;

    // Optional 24ppqn MIDI clock output for external gear (e.g. a Eurorack
    // clock/CV module) to sync to. wasTransportRunning tracks the
    // stopped->running edge so we send Start/Stop at the right moments.
    int clockSamplesRemaining = 0;
    bool wasTransportRunning = false;

    // Cached from kParamSendClock/kParamMaxVoices via OnParamChange -- every
    // other param ProcessBlock reads is cached the same way; these two were
    // the only stragglers still doing a live GetParam() lookup every block.
    bool sendClock = false;
    int maxVoices = 8;

    // When on, ProcessBlock skips evaluating/triggering new hits entirely --
    // whatever's already in activeNotes/pendingNotes keeps ringing out and
    // decaying normally, nothing new gets added. A live-performance-only
    // concept, so it's a plain member here rather than an EuclideanTrack
    // field: MidiExport always renders as if not frozen (a snapshot render
    // freezing mid-way wouldn't mean anything).
    bool freeze = false;

    // Playback tracking
    int last16thNote = -1;

    // Keep track of active notes for Note Offs. Sized well past
    // kParamMaxVoices's ceiling of 16 because pendingNotes doesn't just hold
    // voices: a ratchet burst queues up to 8 slice entries per hit, and with
    // bursts spanning up to 4 steps, several bursts' slices can be queued at
    // once -- 64 slots keeps push() from ever silently dropping a retrigger.
    static constexpr int kMaxVoiceSlots = 64;

    struct ActiveNote {
        int noteNumber;
        int channel;
        int samplesRemaining;
    };
    FixedList<ActiveNote, kMaxVoiceSlots> activeNotes;

    // Notes waiting on a swing/soft-clock delay before their note-on fires.
    struct PendingNote {
        int noteNumber;
        int channel;
        int velocity;
        int samplesUntilOn;
        int gateSamples;
    };
    FixedList<PendingNote, kMaxVoiceSlots> pendingNotes;

    // If `noteNumber` on `channel` is already sounding or queued, kill it
    // (sending a real note-off for an active one) before a new instance of
    // the same pitch goes out. Two live note-ons for the same (pitch,
    // channel) -- which harmony drift or a voicing change can produce -- is
    // a stuck-note hazard on hosts/synths that don't expect it.
    void KillExistingNote(int noteNumber, int channel);

    // How many *distinct* (pitch, channel) voices are sounding or queued.
    // This -- not activeNotes.count + pendingNotes.count -- is what the
    // polyphony cap must compare against: a ratchet burst queues up to 8
    // same-pitch pending entries that sound strictly one-at-a-time, so raw
    // list counts wildly overstate polyphony and made the steal loop kill
    // ringing notes that never needed to go. Allocation-free (stack bitmask),
    // O(n) over two small fixed lists -- fine on the audio thread.
    int CountSoundingVoices() const;

    // Evaluates and (if it fires) emits one 16th-note step. `absoluteStep` is
    // the running 16th-note index since transport start -- stepIndex/loopIndex/
    // rotation-drift/swing all derive from it. Factored out of ProcessBlock so
    // a single block can drive several steps when it straddles more than one
    // 16th boundary (high tempo / large buffer), instead of dropping all but
    // the newest. See ProcessBlock's catch-up loop.
    void ProcessStep(int absoluteStep, double bpm, double sampleRate);

    // A block can legitimately span a few 16th notes at extreme tempo/buffer
    // combos (~4 at 300bpm with an 8192-frame buffer), but a jump larger than
    // this is a transport locate or loop wrap, not a straddle -- catching
    // those up would fire a burst of notes for time that never really elapsed.
    // Past this gap, only the current step is played.
    static constexpr int kMaxCatchupSteps = 8;

    // Reseeds `t`'s generative wander (RNG stream, scale degree, Chaos
    // state) to a fresh starting point. Shared by the Reroll message
    // handler (reseeds the live `track`) and batch export (reseeds each
    // offline `EuclideanTrack` copy beyond the first variation) -- same
    // "fresh take" semantics either way. Not audio-thread code in either
    // caller, so std::random_device is fine.
    static void ReseedTrack(EuclideanTrack& t);

    // Same reset as ReseedTrack, but seeded from a fixed value instead of
    // std::random_device -- see kParamDeterministicExport. Used only by
    // export, never by Reroll (which is supposed to actually be random).
    static void ReseedTrackDeterministic(EuclideanTrack& t, uint32_t seed);
};
