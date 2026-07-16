#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "MidiGenerator.h"
#include <vector>

using namespace iplug;
using namespace igraphics;

class MidiGeneratorPlugin final : public Plugin
{
public:
    MidiGeneratorPlugin(const InstanceInfo& info);

    // This replaces JUCE's prepareToPlay
    void OnReset() override;

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
