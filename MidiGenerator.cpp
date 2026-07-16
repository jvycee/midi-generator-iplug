#include "MidiGenerator.h"
#include "IPlug_include_in_plug_src.h"

MidiGeneratorPlugin::MidiGeneratorPlugin(const InstanceInfo& info)
: Plugin(info, MakeConfig(0, 0)) // 0 parameters, 0 presets for now
{
    // Initialize our track with defaults
    track.steps = 16;
    track.pulses = 5;       
    track.rootNote = 60;    
    track.chordType = ChordType::Minor9th;
    track.voicing = VoicingStyle::Drop2;
    track.chordNotes = 4;
    track.gate = 0.5f;      
    
    rebuildTrackPattern(track);
}

void MidiGeneratorPlugin::OnReset()
{
    last16thNote = -1;
    activeNotes.clear();
}

void MidiGeneratorPlugin::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
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
    ITimeInfo timeInfo;
    if (GetTimeInfo(timeInfo))
    {
        // If the DAW is stopped, reset our counters
        if (!timeInfo.mTransportIsRunning) {
            last16thNote = -1;
            return;
        }

        double bpm = timeInfo.mTempo;
        double ppqPosition = timeInfo.mPPQPos;

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
}
