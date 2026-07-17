#include "MidiGenerator.h"
#include "IPlug_include_in_plug_src.h"
#include "MidiExport.h"
#include <cstdlib>
#include <cstdio>

MidiGenerator::MidiGenerator(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, 0)) // 0 presets for now
{
    // Define parameters for the host and UI
    GetParam(kParamSteps)->InitInt("Steps", 16, 1, 32);
    GetParam(kParamPulses)->InitInt("Pulses", 5, 1, 32);
    GetParam(kParamRotation)->InitInt("Rotation", 0, 0, 31);
    GetParam(kParamSequenceMode)->InitEnum("Sequence Mode", (int)SequenceMode::Euclidean,
        { "Euclidean", "Density", "Chaos" });
    GetParam(kParamSequenceAmount)->InitPercentage("Density/Chaos", 50.);
    GetParam(kParamProbability)->InitPercentage("Probability", 100.);
    GetParam(kParamSwing)->InitPercentage("Swing", 0.);
    GetParam(kParamClockAlign)->InitEnum("Clock Align", (int)ClockAlign::Hard, { "Hard", "Soft" });
    GetParam(kParamClockSoftAmount)->InitPercentage("Soft Amount", 25.);
    GetParam(kParamKey)->InitEnum("Key", 0,
        { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" });
    GetParam(kParamScaleMode)->InitEnum("Scale", (int)ScaleMode::Ionian,
        { "Ionian", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Aeolian", "Locrian" });
    GetParam(kParamRootNote)->InitPitch("Root", 60);
    GetParam(kParamChordType)->InitEnum("Chord Type", (int)ChordType::Minor9th,
        { "Single", "Power",  "Octave",  "Major",  "Minor",
          "Dim",    "Aug",    "Sus2",    "Sus4",   "Maj7",
          "Min7",   "Dom7",   "Min9",    "Maj9",   "Dom9",
          "Min11",  "Maj6",   "Min6",    "HalfDim7","Dim7" });
    GetParam(kParamVoicing)->InitEnum("Voicing", (int)VoicingStyle::Close,
        { "Close", "Drop2", "Drop3", "Spread", "Wide" });
    GetParam(kParamChordVoices)->InitInt("Voices", 4, 1, 6);
    GetParam(kParamHarmonyDrift)->InitPercentage("Drift", 20.);
    GetParam(kParamNoteLength)->InitInt("Note Length", 32, 1, 256); // 16th-note steps; 32 = 2 bars, 256 = 16 bars
    GetParam(kParamGate)->InitPercentage("Gate", 85.);
    GetParam(kParamVelocity)->InitPercentage("Velocity", 60.); // softer default -- high velocity often also brightens/hardens the downstream synth's timbre
    GetParam(kParamMidiChannel)->InitInt("MIDI Channel", 1, 1, 16);
    GetParam(kParamGlobalTranspose)->InitInt("Transpose", 0, -24, 24);
    GetParam(kParamMod1Source)->InitEnum("Mod1 Source", (int)ModSource::Velocity,
        { "Off", "Velocity", "Gate", "Probability", "Dens/Chaos", "Drift", "Random" });
    GetParam(kParamMod1CC)->InitInt("Mod1 CC#", 1, 0, 127);   // CC1 = mod wheel by convention, but any synth can remap it
    GetParam(kParamMod2Source)->InitEnum("Mod2 Source", (int)ModSource::HarmonyDrift,
        { "Off", "Velocity", "Gate", "Probability", "Dens/Chaos", "Drift", "Random" });
    GetParam(kParamMod2CC)->InitInt("Mod2 CC#", 74, 0, 127);  // CC74 = filter cutoff by convention
    GetParam(kParamExportBars)->InitInt("Export Bars", 4, 1, 16);
    GetParam(kParamMonoMode)->InitBool("Mono", false);
    GetParam(kParamMaxVoices)->InitInt("Max Voices", 8, 1, 16); // global cap across overlapping hits, not per-chord

    // Mod3-8: default Off. Unlike Mod1/2 these don't have an obviously
    // "right" default source/CC -- modular patching is deliberate (you wire
    // a specific output to a specific destination), so leave them inert
    // until the user opts a slot in, rather than guessing traffic they
    // didn't ask for.
    static const char* kModSourceNames[] = { "Off", "Velocity", "Gate", "Probability", "Dens/Chaos", "Drift", "Random" };
    static const int kMod38DefaultCC[6] = { 11, 7, 10, 71, 91, 93 }; // Expression, Volume, Pan, Resonance/Timbre, Reverb, Chorus
    const int mod38Params[6][2] = {
        { kParamMod3Source, kParamMod3CC }, { kParamMod4Source, kParamMod4CC },
        { kParamMod5Source, kParamMod5CC }, { kParamMod6Source, kParamMod6CC },
        { kParamMod7Source, kParamMod7CC }, { kParamMod8Source, kParamMod8CC },
    };
    for (int i = 0; i < 6; i++)
    {
        char nameBuf[16];
        snprintf(nameBuf, sizeof(nameBuf), "Mod%d Source", i + 3);
        GetParam(mod38Params[i][0])->InitEnum(nameBuf, (int)ModSource::Off,
            { kModSourceNames[0], kModSourceNames[1], kModSourceNames[2], kModSourceNames[3],
              kModSourceNames[4], kModSourceNames[5], kModSourceNames[6] });
        snprintf(nameBuf, sizeof(nameBuf), "Mod%d CC#", i + 3);
        GetParam(mod38Params[i][1])->InitInt(nameBuf, kMod38DefaultCC[i], 0, 127);
    }

    GetParam(kParamSendClock)->InitBool("Send Clock", false); // off by default -- opt in for modular/external sync

    // UI layout state (Full/Compact), not a musical param -- meta + non-
    // automatable so it stays out of the host's automation lane list, but it
    // still rides the normal per-instance state save/recall (see
    // app.js's onParamStateChanged -> showView()).
    GetParam(kParamUIViewMode)->InitBool("UI View Mode", false, "", IParam::kFlagCannotAutomate | IParam::kFlagMeta, "", "Full", "Compact");

    // Initialize our track from the params
    track.steps = GetParam(kParamSteps)->Int();
    track.pulses = GetParam(kParamPulses)->Int();
    track.rotation = GetParam(kParamRotation)->Int();
    track.rootNote = GetParam(kParamRootNote)->Int();
    track.voicing = static_cast<VoicingStyle>(GetParam(kParamVoicing)->Int());
    track.monoMode = GetParam(kParamMonoMode)->Bool();
    track.noteLengthSteps = GetParam(kParamNoteLength)->Int();
    track.gate = (float)(GetParam(kParamGate)->Value() / 100.);
    track.velocity = (float)(GetParam(kParamVelocity)->Value() / 100.);
    track.midiChannel = GetParam(kParamMidiChannel)->Int();
    track.seqMode = static_cast<SequenceMode>(GetParam(kParamSequenceMode)->Int());
    track.density = track.chaosAmount = (float)(GetParam(kParamSequenceAmount)->Value() / 100.);
    track.probability = (float)(GetParam(kParamProbability)->Value() / 100.);
    track.chordType = static_cast<ChordType>(GetParam(kParamChordType)->Int());
    track.chordNotes = GetParam(kParamChordVoices)->Int();
    track.driftAmount = (float)(GetParam(kParamHarmonyDrift)->Value() / 100.);

    globalParams.globalSwing = (float)(GetParam(kParamSwing)->Value() / 100.);
    globalParams.clockAlign = static_cast<ClockAlign>(GetParam(kParamClockAlign)->Int());
    globalParams.clockSoftAmount = (float)(GetParam(kParamClockSoftAmount)->Value() / 100.);
    globalParams.key = GetParam(kParamKey)->Int();
    globalParams.scaleMode = static_cast<ScaleMode>(GetParam(kParamScaleMode)->Int());
    globalParams.globalTranspose = GetParam(kParamGlobalTranspose)->Int();

    modSlots[0].source = static_cast<ModSource>(GetParam(kParamMod1Source)->Int());
    modSlots[0].ccNumber = GetParam(kParamMod1CC)->Int();
    modSlots[1].source = static_cast<ModSource>(GetParam(kParamMod2Source)->Int());
    modSlots[1].ccNumber = GetParam(kParamMod2CC)->Int();
    for (int i = 0; i < 6; i++)
    {
        modSlots[i + 2].source = static_cast<ModSource>(GetParam(mod38Params[i][0])->Int());
        modSlots[i + 2].ccNumber = GetParam(mod38Params[i][1])->Int();
    }

    rebuildTrackPattern(track);

#ifdef WEBVIEW_EDITOR_DELEGATE
#ifdef DEBUG
    SetEnableDevTools(true);
#endif
    mEditorInitFunc = [&]() {
        LoadIndexHtml(__FILE__, GetBundleID());
        EnableScroll(false);
    };
#endif
}

void MidiGenerator::OnParamChange(int paramIdx)
{
    switch (paramIdx)
    {
        case kParamPulses:
            track.pulses = GetParam(kParamPulses)->Int();
            rebuildTrackPattern(track);
            break;
        case kParamSequenceMode:
            track.seqMode = static_cast<SequenceMode>(GetParam(kParamSequenceMode)->Int());
            break;
        case kParamSequenceAmount:
            track.density = track.chaosAmount = (float)(GetParam(kParamSequenceAmount)->Value() / 100.);
            break;
        case kParamProbability:
            track.probability = (float)(GetParam(kParamProbability)->Value() / 100.);
            break;
        case kParamSwing:
            globalParams.globalSwing = (float)(GetParam(kParamSwing)->Value() / 100.);
            break;
        case kParamClockAlign:
            globalParams.clockAlign = static_cast<ClockAlign>(GetParam(kParamClockAlign)->Int());
            break;
        case kParamClockSoftAmount:
            globalParams.clockSoftAmount = (float)(GetParam(kParamClockSoftAmount)->Value() / 100.);
            break;
        case kParamKey:
            globalParams.key = GetParam(kParamKey)->Int();
            break;
        case kParamScaleMode:
            globalParams.scaleMode = static_cast<ScaleMode>(GetParam(kParamScaleMode)->Int());
            break;
        case kParamChordType:
            track.chordType = static_cast<ChordType>(GetParam(kParamChordType)->Int());
            break;
        case kParamChordVoices:
            track.chordNotes = GetParam(kParamChordVoices)->Int();
            break;
        case kParamHarmonyDrift:
            track.driftAmount = (float)(GetParam(kParamHarmonyDrift)->Value() / 100.);
            break;
        case kParamSteps:
            track.steps = GetParam(kParamSteps)->Int();
            rebuildTrackPattern(track);
            break;
        case kParamRotation:
            track.rotation = GetParam(kParamRotation)->Int();
            rebuildTrackPattern(track);
            break;
        case kParamRootNote:
            track.rootNote = GetParam(kParamRootNote)->Int();
            break;
        case kParamVoicing:
            track.voicing = static_cast<VoicingStyle>(GetParam(kParamVoicing)->Int());
            break;
        case kParamMonoMode:
            track.monoMode = GetParam(kParamMonoMode)->Bool();
            break;
        case kParamNoteLength:
            track.noteLengthSteps = GetParam(kParamNoteLength)->Int();
            break;
        case kParamGate:
            track.gate = (float)(GetParam(kParamGate)->Value() / 100.);
            break;
        case kParamVelocity:
            track.velocity = (float)(GetParam(kParamVelocity)->Value() / 100.);
            break;
        case kParamMidiChannel:
            track.midiChannel = GetParam(kParamMidiChannel)->Int();
            break;
        case kParamGlobalTranspose:
            globalParams.globalTranspose = GetParam(kParamGlobalTranspose)->Int();
            break;
        case kParamMod1Source:
            modSlots[0].source = static_cast<ModSource>(GetParam(kParamMod1Source)->Int());
            break;
        case kParamMod1CC:
            modSlots[0].ccNumber = GetParam(kParamMod1CC)->Int();
            break;
        case kParamMod2Source:
            modSlots[1].source = static_cast<ModSource>(GetParam(kParamMod2Source)->Int());
            break;
        case kParamMod2CC:
            modSlots[1].ccNumber = GetParam(kParamMod2CC)->Int();
            break;
        case kParamMod3Source: modSlots[2].source = static_cast<ModSource>(GetParam(kParamMod3Source)->Int()); break;
        case kParamMod3CC:     modSlots[2].ccNumber = GetParam(kParamMod3CC)->Int(); break;
        case kParamMod4Source: modSlots[3].source = static_cast<ModSource>(GetParam(kParamMod4Source)->Int()); break;
        case kParamMod4CC:     modSlots[3].ccNumber = GetParam(kParamMod4CC)->Int(); break;
        case kParamMod5Source: modSlots[4].source = static_cast<ModSource>(GetParam(kParamMod5Source)->Int()); break;
        case kParamMod5CC:     modSlots[4].ccNumber = GetParam(kParamMod5CC)->Int(); break;
        case kParamMod6Source: modSlots[5].source = static_cast<ModSource>(GetParam(kParamMod6Source)->Int()); break;
        case kParamMod6CC:     modSlots[5].ccNumber = GetParam(kParamMod6CC)->Int(); break;
        case kParamMod7Source: modSlots[6].source = static_cast<ModSource>(GetParam(kParamMod7Source)->Int()); break;
        case kParamMod7CC:     modSlots[6].ccNumber = GetParam(kParamMod7CC)->Int(); break;
        case kParamMod8Source: modSlots[7].source = static_cast<ModSource>(GetParam(kParamMod8Source)->Int()); break;
        case kParamMod8CC:     modSlots[7].ccNumber = GetParam(kParamMod8CC)->Int(); break;
        default:
            break;
    }
}

bool MidiGenerator::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
    if (msgTag == 100) // MIDI export request (SAMFUI(100) from app.js's requestExport())
    {
        WDL_String path = ExportPatternAsMidiFile();
        if (path.GetLength() > 0)
        {
            // NO_IGRAPHICS means there's no IGraphics::InitiateExternalFileDragDrop()
            // to hand this off as a native OS drag session (that API lives on
            // IGraphics, which doesn't exist in a WebView-only build). Reveal
            // it in Finder instead so the export is still one click away from
            // being dragged into the DAW by hand.
            WDL_String cmd;
            cmd.SetFormatted(1200, "open -R \"%s\"", path.Get());
            system(cmd.Get());
            SendArbitraryMsgFromDelegate(101 /* export succeeded */);
        }
        else
        {
            SendArbitraryMsgFromDelegate(102 /* export failed */);
        }
        return true;
    }

    return false;
}

void MidiGenerator::OnReset()
{
    last16thNote = -1;
    activeNotes.clear();
    pendingNotes.clear();
    ccUpdateSamplesRemaining = 0;
    clockSamplesRemaining = 0;
    wasTransportRunning = false;
}

void MidiGenerator::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
    for (auto it = pendingNotes.begin(); it != pendingNotes.end(); )
    {
        it->samplesUntilOn -= nFrames;
        if (it->samplesUntilOn <= 0)
        {
            IMidiMsg msg;
            msg.MakeNoteOnMsg(it->noteNumber, it->velocity, 0, it->channel - 1);
            SendMidiMsg(msg);
            activeNotes.push_back({it->noteNumber, it->channel, it->gateSamples});
            it = pendingNotes.erase(it);
        }
        else ++it;
    }

    for (auto it = activeNotes.begin(); it != activeNotes.end(); )
    {
        it->samplesRemaining -= nFrames;
        if (it->samplesRemaining <= 0)
        {
            IMidiMsg msg;
            msg.MakeNoteOffMsg(it->noteNumber, 0, it->channel - 1);
            SendMidiMsg(msg);
            it = activeNotes.erase(it);
        }
        else ++it;
    }

    bool transportRunning = GetTransportIsRunning();
    bool sendClock = GetParam(kParamSendClock)->Bool();

    if (sendClock && transportRunning != wasTransportRunning)
    {
        IMidiMsg edgeMsg(0, transportRunning ? (uint8_t)0xFA /* Start */ : (uint8_t)0xFC /* Stop */);
        SendMidiMsg(edgeMsg);
        if (transportRunning) clockSamplesRemaining = 0; // fire the first tick right away
    }
    wasTransportRunning = transportRunning;

    if (!transportRunning) {
        last16thNote = -1;
        return;
    }

    double bpm = GetTempo();
    double sampleRate = GetSampleRate();

    // Continuous CC ticker (~20ms / 50Hz), independent of whether a step
    // triggers this block. Real CV wants to move smoothly, not just step
    // when a note happens to fire -- see the modSlots comment in the header.
    int ccIntervalSamples = std::max(1, (int)(0.02 * sampleRate));
    ccUpdateSamplesRemaining -= nFrames;
    while (ccUpdateSamplesRemaining <= 0)
    {
        ccUpdateSamplesRemaining += ccIntervalSamples;
        for (const ModSlot& slot : modSlots)
        {
            if (slot.source == ModSource::Off) continue;
            float value = evaluateModSource(track, slot.source);
            IMidiMsg ccMsg;
            ccMsg.MakeControlChangeMsg(static_cast<IMidiMsg::EControlChangeMsg>(slot.ccNumber), value, track.midiChannel - 1);
            SendMidiMsg(ccMsg);
        }
    }

    // 24ppqn MIDI clock, for external gear (e.g. a Eurorack clock/CV
    // interface over USB) to sync to this plugin's tempo.
    if (sendClock)
    {
        int clockIntervalSamples = std::max(1, (int)((60.0 / std::max(1.0, bpm) / 24.0) * sampleRate));
        clockSamplesRemaining -= nFrames;
        while (clockSamplesRemaining <= 0)
        {
            clockSamplesRemaining += clockIntervalSamples;
            IMidiMsg clockMsg(0, 0xF8);
            SendMidiMsg(clockMsg);
        }
    }

    double ppqPosition = GetPPQPos();
    int current16thNote = static_cast<int>(ppqPosition * 4.0);

    if (current16thNote != last16thNote && current16thNote >= 0)
    {
        last16thNote = current16thNote;
        int stepIndex = current16thNote % track.steps;
        if (evaluateStepTrigger(track, stepIndex))
        {
            applyHarmonyDrift(track);
            int scaleRoot = scaleDegreeToNote(globalParams.key, globalParams.scaleMode, track.scaleDegree, track.rootNote);
            auto notes = buildTrackNotes(track, scaleRoot, globalParams.key, globalParams.scaleMode);

            if (globalParams.globalTranspose != 0)
            {
                for (int& n : notes) n = foldIntoMidiRange(n + globalParams.globalTranspose);
                std::sort(notes.begin(), notes.end());
                notes.erase(std::unique(notes.begin(), notes.end()), notes.end());
            }

            // Global polyphony cap. Voices (chordNotes) already limits a single
            // hit's chord to at most 6 notes, but with long noteLengthSteps
            // several hits' chords stack on top of each other while they're
            // all still ringing -- that's the actual source of more voices
            // than the Voices knob would suggest. Make room oldest-first
            // (activeNotes before never-yet-triggered pendingNotes) rather
            // than just dropping the new hit, so the sequence still feels
            // continuous instead of periodically going silent.
            int maxVoices = std::max(1, GetParam(kParamMaxVoices)->Int());
            if ((int)notes.size() > maxVoices)
                notes.resize(maxVoices);

            int totalVoices = (int)activeNotes.size() + (int)pendingNotes.size();
            while (totalVoices + (int)notes.size() > maxVoices && !activeNotes.empty())
            {
                IMidiMsg offMsg;
                offMsg.MakeNoteOffMsg(activeNotes.front().noteNumber, 0, activeNotes.front().channel - 1);
                SendMidiMsg(offMsg);
                activeNotes.erase(activeNotes.begin());
                --totalVoices;
            }
            while (totalVoices + (int)notes.size() > maxVoices && !pendingNotes.empty())
            {
                pendingNotes.erase(pendingNotes.begin()); // never got a note-on, just drop it
                --totalVoices;
            }

            double sixteenthNoteSeconds = (60.0 / bpm) * 0.25;
            int sixteenthNoteSamples = static_cast<int>(sixteenthNoteSeconds * sampleRate);
            double noteSlotSeconds = sixteenthNoteSeconds * std::max(1, track.noteLengthSteps);
            int gateSamples = static_cast<int>(noteSlotSeconds * track.gate * sampleRate);
            // Subtle downward-only velocity humanize: every hit landing at
            // identically full strength is a big part of what reads as
            // "stabby" rather than "dreamy" -- real playing (and real pads)
            // breathe. Never louder than the knob setting, just sometimes
            // softer, so raising Velocity still sets a reliable ceiling.
            float humanizedVelocity = track.velocity * (1.0f - randUnit01(track.rngState) * 0.2f);
            int velocity = std::max(1, static_cast<int>(humanizedVelocity * 127.0f));
            int timingOffset = computeTimingOffsetSamples(track, current16thNote, globalParams, sixteenthNoteSamples);

            for (const ModSlot& slot : modSlots)
            {
                if (slot.source == ModSource::Off) continue;
                float value = evaluateModSource(track, slot.source);
                IMidiMsg ccMsg;
                ccMsg.MakeControlChangeMsg(static_cast<IMidiMsg::EControlChangeMsg>(slot.ccNumber), value, track.midiChannel - 1);
                SendMidiMsg(ccMsg);
            }

            for (int note : notes)
            {
                if (timingOffset <= 0)
                {
                    IMidiMsg msg;
                    msg.MakeNoteOnMsg(note, velocity, 0, track.midiChannel - 1);
                    SendMidiMsg(msg);
                    activeNotes.push_back({note, track.midiChannel, gateSamples});
                }
                else
                {
                    pendingNotes.push_back({note, track.midiChannel, velocity, timingOffset, gateSamples});
                }
            }
        }
    }
}

WDL_String MidiGenerator::ExportPatternAsMidiFile()
{
    EuclideanTrack trackCopy = track;
    int numBars = GetParam(kParamExportBars)->Int();
    double bpm = GetTempo();
    if (bpm <= 0.0) bpm = 120.0;

    int maxVoices = std::max(1, GetParam(kParamMaxVoices)->Int());
    auto bytes = MidiExport::renderPatternToSMF(trackCopy, globalParams, bpm, numBars, modSlots, 8, maxVoices);

    const char* tmpDir = std::getenv("TMPDIR");
    if (!tmpDir || !*tmpDir) tmpDir = "/tmp";

    WDL_String path;
    path.SetFormatted(1024, "%s/MidiGenerator_Export.mid", tmpDir);

    if (!MidiExport::writeSMFFile(bytes, path.Get()))
        return WDL_String();

    return path;
}
