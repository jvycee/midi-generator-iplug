#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "MidiGeneratorLogic.h"

using namespace iplug;
using namespace igraphics;

const int kNumParams = 1;

enum EParams
{
  kParamPulses = 0
};

class MidiGenerator final : public Plugin
{
public:
    MidiGenerator(const InstanceInfo& info);

    // This replaces JUCE's prepareToPlay
    void OnReset() override;

    // Triggered whenever a UI knob or host automation changes
    void OnParamChange(int paramIdx) override;

    // This replaces JUCE's processBlock
    void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;

private:
    // Our core generator state
    EuclideanTrack track;
    
    // Playback tracking
    int last16thNote = -1;

    // Keep track of active notes for Note Offs
    struct ActiveNote {
        int noteNumber;
        int channel;
        int samplesRemaining;
    };
    std::vector<ActiveNote> activeNotes;
};
