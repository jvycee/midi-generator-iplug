#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "MidiGeneratorLogic.h"

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
  kNumParams
};

class MidiGenerator final : public Plugin
{
public:
    MidiGenerator(const InstanceInfo& info);

    // This replaces JUCE's prepareToPlay
    void OnReset() override;

    // Triggered whenever a UI knob or host automation changes
    void OnParamChange(int paramIdx) override;

    bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;

    // This replaces JUCE's processBlock
    void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;

    // Renders the current pattern (kParamExportBars bars, current tempo) to
    // a temp .mid file and returns its path (empty on failure). Called from
    // OnMessage() in response to the WebView UI's export button (msgTag 100)
    // -- see MidiExport.h for the actual render/write.
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

    // Optional 24ppqn MIDI clock output for external gear (e.g. a Eurorack
    // clock/CV module) to sync to. wasTransportRunning tracks the
    // stopped->running edge so we send Start/Stop at the right moments.
    int clockSamplesRemaining = 0;
    bool wasTransportRunning = false;

    // Playback tracking
    int last16thNote = -1;

    // Keep track of active notes for Note Offs
    struct ActiveNote {
        int noteNumber;
        int channel;
        int samplesRemaining;
    };
    std::vector<ActiveNote> activeNotes;

    // Notes waiting on a swing/soft-clock delay before their note-on fires.
    struct PendingNote {
        int noteNumber;
        int channel;
        int velocity;
        int samplesUntilOn;
        int gateSamples;
    };
    std::vector<PendingNote> pendingNotes;
};
