#include "MidiGenerator.h"
#include "IPlug_include_in_plug_src.h"
#include "IControls.h"

MidiGenerator::MidiGenerator(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, 0)) // 1 parameter, 0 presets for now
{
    // Define our "Pulses" parameter for the host and UI
    GetParam(kParamPulses)->InitInt("Pulses", 5, 1, 16);

    // Initialize our track with defaults
    track.steps = 16;
    track.pulses = GetParam(kParamPulses)->Int();       
    track.rootNote = 60;    
    track.chordType = ChordType::Minor9th;
    track.voicing = VoicingStyle::Drop2;
    track.chordNotes = 4;
    track.gate = 0.5f;      
    
    rebuildTrackPattern(track);

#if IPLUG_EDITOR
    mMakeGraphicsFunc = [&]() {
        return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
    };
  
    mLayoutFunc = [&](IGraphics* pGraphics) {
        pGraphics->AttachCornerResizer(EUIResizerMode::Scale, false);
        pGraphics->AttachPanelBackground(COLOR_DARK_GRAY);
        pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);
        
        const IRECT b = pGraphics->GetBounds();
        
        // Add a title
        pGraphics->AttachControl(new ITextControl(b.GetMidVPadded(50).GetVShifted(-100), "Euclidean MIDI Generator", IText(24, COLOR_WHITE)));
        
        // Add a knob for Pulses!
        pGraphics->AttachControl(new IVKnobControl(b.GetCentredInside(100), kParamPulses, "Pulses"));
    };
#endif
}

void MidiGenerator::OnParamChange(int paramIdx)
{
    switch (paramIdx)
    {
        case kParamPulses:
            // When the knob turns, update our math engine
            track.pulses = GetParam(kParamPulses)->Int();
            rebuildTrackPattern(track);
            break;
        default:
            break;
    }
}

void MidiGenerator::OnReset()
{
    last16thNote = -1;
    activeNotes.clear();
}

void MidiGenerator::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
    // 1. Process Note Offs for currently active notes
    for (auto it = activeNotes.begin(); it != activeNotes.end(); )
    {
        it->samplesRemaining -= nFrames;
        
        if (it->samplesRemaining <= 0)
        {
            IMidiMsg msg;
            // iPlug2 channels are 0-indexed (0-15)
            msg.MakeNoteOffMsg(it->noteNumber, 0, it->channel - 1); 
            SendMidiMsg(msg);
            
            it = activeNotes.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // 2. Ask Logic Pro's host about the current time/transport state
    // If the DAW is stopped, reset our counters
    if (!GetTransportIsRunning()) {
        last16thNote = -1;
        return;
    }

    double bpm = GetTempo();
    double ppqPosition = GetPPQPos();

        // PPQ (Pulses Per Quarter note). 1 PPQ = 1 beat.
        // Therefore, 16th notes happen every 0.25 PPQ.
        int current16thNote = static_cast<int>(ppqPosition * 4.0);

        // 3. Did we just cross a 16th note boundary?
        if (current16thNote != last16thNote && current16thNote >= 0)
        {
            last16thNote = current16thNote;

            // Map continuous time to our step sequence (0-15)
            int stepIndex = current16thNote % track.steps;

            // 4. If there's a hit on this step, generate the MIDI!
            if (track.pattern[stepIndex])
            {
                auto notes = buildChord(track.rootNote, track.chordType, track.voicing, track.chordNotes, track.chordSpread);
                
                double sampleRate = GetSampleRate();
                double sixteenthNoteSeconds = (60.0 / bpm) * 0.25;
                int gateSamples = static_cast<int>(sixteenthNoteSeconds * track.gate * sampleRate);

                int velocity = static_cast<int>(track.velocity * 127.0f);
                
                for (int note : notes)
                {
                    IMidiMsg msg;
                    // Args: Note, Velocity, SampleOffset, Channel
                    msg.MakeNoteOnMsg(note, velocity, 0, track.midiChannel - 1);
                    SendMidiMsg(msg);
                    
                    activeNotes.push_back({note, track.midiChannel, gateSamples});
                }
            }
        }
}
