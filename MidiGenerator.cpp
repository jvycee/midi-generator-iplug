#include "MidiGenerator.h"
#include "IPlug_include_in_plug_src.h"
#include "MidiExport.h"
#include <cstdlib>
#include <cstdio>
#include <random>

namespace
{
    // Single source of truth for the 8 mod slots' param IDs and defaults --
    // shared by the constructor (initial InitEnum/InitInt + track sync) and
    // OnParamChange (live updates), so adding a 9th slot means touching one
    // table instead of two hand-written call sites.
    struct ModSlotParamIds { int srcParam; int ccParam; };

    constexpr ModSlotParamIds kModSlotParams[8] = {
        { kParamMod1Source, kParamMod1CC }, { kParamMod2Source, kParamMod2CC },
        { kParamMod3Source, kParamMod3CC }, { kParamMod4Source, kParamMod4CC },
        { kParamMod5Source, kParamMod5CC }, { kParamMod6Source, kParamMod6CC },
        { kParamMod7Source, kParamMod7CC }, { kParamMod8Source, kParamMod8CC },
    };

    // Mod1/2 default to something musically useful out of the box; Mod3-8
    // default Off since modular patching is deliberate -- see the header
    // comment on modSlots for why we don't guess traffic the user didn't ask for.
    constexpr int kModSlotDefaultSource[8] = {
        (int)ModSource::Velocity, (int)ModSource::HarmonyDrift,
        (int)ModSource::Off, (int)ModSource::Off, (int)ModSource::Off,
        (int)ModSource::Off, (int)ModSource::Off, (int)ModSource::Off,
    };
    // CC1 = mod wheel, CC74 = filter cutoff by convention; the rest
    // (Expression, Volume, Pan, Resonance/Timbre, Reverb, Chorus) are just
    // sensible starting points a modular patch is free to reassign.
    constexpr int kModSlotDefaultCC[8] = { 1, 74, 11, 7, 10, 71, 91, 93 };
}

MidiGenerator::MidiGenerator(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, 5)) // 5 factory presets, see the MakePresetFromNamedParams calls below
{
    // Define parameters for the host and UI
    GetParam(kParamSteps)->InitInt("Steps", 16, 1, 32);
    GetParam(kParamPulses)->InitInt("Pulses", 5, 1, 32);
    GetParam(kParamRotation)->InitInt("Rotation", 0, 0, 31);
    GetParam(kParamPatternInvert)->InitBool("Pattern Invert", false);
    GetParam(kParamTrigEvery)->InitInt("Trig Every", 1, 1, 8);
    GetParam(kParamTrigOffset)->InitInt("Trig Offset", 0, 0, 7);
    GetParam(kParamRotationDriftPeriod)->InitInt("Rotation Drift", 0, 0, 128);
    GetParam(kParamRatchetCount)->InitInt("Ratchet", 1, 1, 8);
    GetParam(kParamChordPriority)->InitBool("Chord Priority", false, "", 0, "", "Scale", "Chord");
    GetParam(kParamDriftGravity)->InitPercentage("Drift Gravity", 0.);
    GetParam(kParamAccentEvery)->InitInt("Accent Every", 4, 1, 8);
    GetParam(kParamAccentAmount)->InitPercentage("Accent Amount", 0.);
    GetParam(kParamFreeze)->InitBool("Freeze", false);
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
        { "Ionian", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Aeolian", "Locrian",
          "Harmonic Minor", "Melodic Minor", "Byzantine", "Persian", "Neapolitan Minor", "Neapolitan Major", "Hungarian Minor" });
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
    GetParam(kParamExportBars)->InitInt("Export Bars", 4, 1, 16);
    GetParam(kParamExportVariations)->InitInt("Variations", 1, 1, 8);
    GetParam(kParamMonoMode)->InitBool("Mono", false);
    GetParam(kParamMaxVoices)->InitInt("Max Voices", 8, 1, 16); // global cap across overlapping hits, not per-chord

    for (int i = 0; i < 8; ++i)
    {
        char nameBuf[16];
        snprintf(nameBuf, sizeof(nameBuf), "Mod%d Source", i + 1);
        GetParam(kModSlotParams[i].srcParam)->InitEnum(nameBuf, kModSlotDefaultSource[i],
            { "Off", "Velocity", "Gate", "Probability", "Dens/Chaos", "Drift", "Random" });
        snprintf(nameBuf, sizeof(nameBuf), "Mod%d CC#", i + 1);
        GetParam(kModSlotParams[i].ccParam)->InitInt(nameBuf, kModSlotDefaultCC[i], 0, 127);
    }

    GetParam(kParamSendClock)->InitBool("Send Clock", false); // off by default -- opt in for modular/external sync

    // UI layout state (Full/Compact), not a musical param -- meta + non-
    // automatable so it stays out of the host's automation lane list, but it
    // still rides the normal per-instance state save/recall (see
    // app.js's onParamStateChanged -> showView()).
    GetParam(kParamUIViewMode)->InitBool("UI View Mode", false, "", IParam::kFlagCannotAutomate | IParam::kFlagMeta, "", "Full", "Compact");

    // Factory presets. MakePresetFromNamedParams only needs the params that
    // differ from default -- everything else falls back to GetParam(i)->
    // Value() as already set by the InitXxx calls above, which is why this
    // has to come after all of them. Param values are untyped varargs: int
    // for bool/int/enum params, double (with an explicit decimal) for
    // percentage params -- see GET_PARAM_FROM_VARARG in IPlugUtilities.h.
    MakePresetFromNamedParams("Ambient Drift", 14,
        kParamSteps, 16, kParamPulses, 5,
        kParamChordType, (int)ChordType::Minor9th, kParamVoicing, (int)VoicingStyle::Drop2, kParamChordVoices, 4,
        kParamHarmonyDrift, 35., kParamDriftGravity, 20.,
        kParamNoteLength, 64, kParamGate, 90., kParamVelocity, 55.,
        kParamRotationDriftPeriod, 24,
        kParamClockAlign, (int)ClockAlign::Soft, kParamClockSoftAmount, 30., kParamSwing, 10.);

    MakePresetFromNamedParams("Techno Hats", 11,
        kParamMonoMode, 1, kParamSteps, 16, kParamPulses, 11, kParamRootNote, 79,
        kParamNoteLength, 2, kParamGate, 40., kParamVelocity, 70., kParamSwing, 15.,
        kParamAccentEvery, 4, kParamAccentAmount, 35., kParamProbability, 90.);

    MakePresetFromNamedParams("Bass Pulse", 10,
        kParamMonoMode, 1, kParamSteps, 8, kParamPulses, 4, kParamRootNote, 36,
        kParamNoteLength, 4, kParamGate, 70., kParamVelocity, 80.,
        kParamAccentEvery, 4, kParamAccentAmount, 25.,
        kParamScaleMode, (int)ScaleMode::Aeolian);

    MakePresetFromNamedParams("Generative Chords", 12,
        kParamSequenceMode, (int)SequenceMode::Chaos, kParamSequenceAmount, 55.,
        kParamChordType, (int)ChordType::Dom9th, kParamVoicing, (int)VoicingStyle::Spread, kParamChordVoices, 5,
        kParamChordPriority, 1,
        kParamHarmonyDrift, 25., kParamDriftGravity, 40.,
        kParamNoteLength, 48, kParamGate, 80., kParamVelocity, 50.,
        kParamScaleMode, (int)ScaleMode::Dorian);

    MakePresetFromNamedParams("Sparse Bells", 11,
        kParamMonoMode, 1, kParamSteps, 16, kParamPulses, 3,
        kParamRotationDriftPeriod, 40, kParamTrigEvery, 3, kParamTrigOffset, 1,
        kParamNoteLength, 96, kParamGate, 95., kParamVelocity, 45.,
        kParamRootNote, 84, kParamScaleMode, (int)ScaleMode::Lydian);

    // Initialize our track from the params
    track.steps = GetParam(kParamSteps)->Int();
    track.pulses = GetParam(kParamPulses)->Int();
    track.rotation = GetParam(kParamRotation)->Int();
    track.patternInvert = GetParam(kParamPatternInvert)->Bool();
    track.trigEvery = GetParam(kParamTrigEvery)->Int();
    track.trigOffset = GetParam(kParamTrigOffset)->Int();
    track.rotationDriftPeriod = GetParam(kParamRotationDriftPeriod)->Int();
    track.ratchetCount = GetParam(kParamRatchetCount)->Int();
    track.chordPriority = GetParam(kParamChordPriority)->Bool();
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
    track.driftGravity = (float)(GetParam(kParamDriftGravity)->Value() / 100.);
    track.accentEvery = GetParam(kParamAccentEvery)->Int();
    track.accentAmount = (float)(GetParam(kParamAccentAmount)->Value() / 100.);

    globalParams.globalSwing = (float)(GetParam(kParamSwing)->Value() / 100.);
    globalParams.clockAlign = static_cast<ClockAlign>(GetParam(kParamClockAlign)->Int());
    globalParams.clockSoftAmount = (float)(GetParam(kParamClockSoftAmount)->Value() / 100.);
    globalParams.key = GetParam(kParamKey)->Int();
    globalParams.scaleMode = static_cast<ScaleMode>(GetParam(kParamScaleMode)->Int());
    globalParams.globalTranspose = GetParam(kParamGlobalTranspose)->Int();

    sendClock = GetParam(kParamSendClock)->Bool();
    maxVoices = std::max(1, GetParam(kParamMaxVoices)->Int());
    freeze = GetParam(kParamFreeze)->Bool();

    for (int i = 0; i < 8; ++i)
    {
        modSlots[i].source = static_cast<ModSource>(GetParam(kModSlotParams[i].srcParam)->Int());
        modSlots[i].ccNumber = GetParam(kModSlotParams[i].ccParam)->Int();
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
    for (int i = 0; i < 8; ++i)
    {
        if (paramIdx == kModSlotParams[i].srcParam)
        {
            modSlots[i].source = static_cast<ModSource>(GetParam(paramIdx)->Int());
            return;
        }
        if (paramIdx == kModSlotParams[i].ccParam)
        {
            modSlots[i].ccNumber = GetParam(paramIdx)->Int();
            return;
        }
    }

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
        case kParamDriftGravity:
            track.driftGravity = (float)(GetParam(kParamDriftGravity)->Value() / 100.);
            break;
        case kParamAccentEvery:
            track.accentEvery = GetParam(kParamAccentEvery)->Int();
            break;
        case kParamAccentAmount:
            track.accentAmount = (float)(GetParam(kParamAccentAmount)->Value() / 100.);
            break;
        case kParamFreeze:
            freeze = GetParam(kParamFreeze)->Bool();
            break;
        case kParamSteps:
            track.steps = GetParam(kParamSteps)->Int();
            rebuildTrackPattern(track);
            break;
        case kParamRotation:
            track.rotation = GetParam(kParamRotation)->Int();
            rebuildTrackPattern(track);
            break;
        case kParamPatternInvert:
            track.patternInvert = GetParam(kParamPatternInvert)->Bool();
            rebuildTrackPattern(track);
            break;
        case kParamTrigEvery:
            track.trigEvery = GetParam(kParamTrigEvery)->Int();
            break;
        case kParamTrigOffset:
            track.trigOffset = GetParam(kParamTrigOffset)->Int();
            break;
        case kParamRotationDriftPeriod:
            track.rotationDriftPeriod = GetParam(kParamRotationDriftPeriod)->Int();
            break;
        case kParamRatchetCount:
            track.ratchetCount = GetParam(kParamRatchetCount)->Int();
            break;
        case kParamChordPriority:
            track.chordPriority = GetParam(kParamChordPriority)->Bool();
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
        case kParamSendClock:
            sendClock = GetParam(kParamSendClock)->Bool();
            break;
        case kParamMaxVoices:
            maxVoices = std::max(1, GetParam(kParamMaxVoices)->Int());
            break;
        default:
            break;
    }
}

bool MidiGenerator::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
    if (msgTag == kMsgTagExportRequest) // from app.js's requestExport()
    {
        WDL_String path = ExportPatternAsMidiFile();
        if (path.GetLength() > 0)
        {
#ifdef OS_MAC
            // NO_IGRAPHICS means there's no IGraphics::InitiateExternalFileDragDrop()
            // to hand this off as a native OS drag session (that API lives on
            // IGraphics, which doesn't exist in a WebView-only build). Reveal
            // it in Finder as a fallback so the export is always at least one
            // click away from being dragged into the DAW by hand. Mac-only:
            // there's no Finder (or `open`) to shell out to on iOS.
            WDL_String cmd;
            cmd.SetFormatted(1200, "open -R \"%s\"", path.Get());
            system(cmd.Get());
#endif
            // Also send the resolved path back so app.js can wire up a real
            // OS-level drag via WebKit's DownloadURL drag flavor (see
            // requestExport()/onExportResult() -- the same mechanism sites
            // like Gmail use for "drag this attachment to your desktop").
            // Unverified against a real host/DAW in this environment; the
            // Finder reveal above is the safety net if it doesn't pan out.
            //
            // WebViewEditorDelegate's override of this hides IEditorDelegate's
            // default arguments (defaults aren't inherited across an
            // override) -- dataSize/pData must be passed explicitly here.
            SendArbitraryMsgFromDelegate(kMsgTagExportSucceeded, path.GetLength(), path.Get());
        }
        else
        {
            SendArbitraryMsgFromDelegate(kMsgTagExportFailed, 0, nullptr);
        }
        return true;
    }

    if (msgTag == kMsgTagRerollRequest) // from app.js's requestReroll()
    {
        ReseedTrack(track);
        return true;
    }

    return false;
}

// static
void MidiGenerator::ReseedTrack(EuclideanTrack& t)
{
    // A new RNG stream, scale degree back to the tonic, and a fresh (non-
    // fixed-point) logistic-map state for Chaos mode -- gives an actually
    // fresh starting point rather than a fixed one that'd make every reseed
    // retrace the exact same "random" path.
    std::random_device rd;
    t.rngState = rd();
    if (t.rngState == 0) t.rngState = 0x9E3779B9u; // xorshift32 guards 0 too, but avoid relying on it twice
    t.scaleDegree = 0;
    t.chaosX = 0.1 + (double)(rd() % 8000) / 10000.0; // fresh value in [0.1, 0.9), clear of both fixed points
}

void MidiGenerator::OnIdle()
{
    double bpm = GetTempo();
    if (std::abs(bpm - lastSentTempo) > 0.001)
    {
        lastSentTempo = bpm;
        WDL_String bpmStr;
        bpmStr.SetFormatted(32, "%.3f", bpm);
        SendArbitraryMsgFromDelegate(kMsgTagTempoUpdate, bpmStr.GetLength(), bpmStr.Get());
    }
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

void MidiGenerator::SendModCCs()
{
    for (int i = 0; i < 8; ++i)
    {
        const ModSlot& slot = modSlots[i];
        if (slot.source == ModSource::Off) continue;
        float value = evaluateModSource(track, slot.source);

        // Random is meant to move continuously; everything else only
        // actually changes on a new hit, so between hits the 50Hz ticker
        // would otherwise resend an unchanged value every tick -- flooding
        // whatever's downstream with identical CC messages.
        int quantized = std::clamp((int)(value * 127.0f), 0, 127);
        if (slot.source != ModSource::Random && quantized == lastSentCCValue[i])
            continue;
        lastSentCCValue[i] = quantized;

        IMidiMsg ccMsg;
        ccMsg.MakeControlChangeMsg(static_cast<IMidiMsg::EControlChangeMsg>(slot.ccNumber), value, track.midiChannel - 1);
        SendMidiMsg(ccMsg);
    }
}

void MidiGenerator::KillExistingNote(int noteNumber, int channel)
{
    // Remove *every* still-sounding/queued instance of this (pitch, channel),
    // not just the first. A ratchet burst pushes several same-pitch entries
    // at once, so the old "unique by construction, break after one" assumption
    // no longer holds -- stopping at the first match would orphan the rest,
    // leaving them to fire spurious note-ons/offs later (a stuck-note hazard).
    // One note-off releases the pitch regardless of how many note-ons went
    // out; the point of the loop is to clean up our own bookkeeping so no
    // leftover entry emits a stray event.
    bool killedActive = false;
    for (int i = 0; i < activeNotes.count; )
    {
        if (activeNotes.items[i].noteNumber == noteNumber && activeNotes.items[i].channel == channel)
        {
            activeNotes.removeAt(i);
            killedActive = true;
        }
        else ++i;
    }
    if (killedActive)
    {
        IMidiMsg off;
        off.MakeNoteOffMsg(noteNumber, 0, channel - 1);
        SendMidiMsg(off);
    }

    for (int i = 0; i < pendingNotes.count; )
    {
        if (pendingNotes.items[i].noteNumber == noteNumber && pendingNotes.items[i].channel == channel)
            pendingNotes.removeAt(i); // never got a note-on, just drop it
        else ++i;
    }
}

void MidiGenerator::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
    for (int i = 0; i < pendingNotes.count; )
    {
        PendingNote& p = pendingNotes.items[i];
        p.samplesUntilOn -= nFrames;
        if (p.samplesUntilOn <= 0)
        {
            IMidiMsg msg;
            msg.MakeNoteOnMsg(p.noteNumber, p.velocity, 0, p.channel - 1);
            SendMidiMsg(msg);
            activeNotes.push({ p.noteNumber, p.channel, p.gateSamples });
            pendingNotes.removeAt(i);
        }
        else ++i;
    }

    for (int i = 0; i < activeNotes.count; )
    {
        ActiveNote& a = activeNotes.items[i];
        a.samplesRemaining -= nFrames;
        if (a.samplesRemaining <= 0)
        {
            IMidiMsg msg;
            msg.MakeNoteOffMsg(a.noteNumber, 0, a.channel - 1);
            SendMidiMsg(msg);
            activeNotes.removeAt(i);
        }
        else ++i;
    }

    bool transportRunning = GetTransportIsRunning();

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
        SendModCCs();
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
        int loopIndex = current16thNote / track.steps;
        int rotationDriftSteps = track.rotationDriftPeriod > 0 ? (current16thNote / track.rotationDriftPeriod) : 0;
        // !freeze short-circuits before any RNG draw (trig/density/chaos/
        // probability), so Freeze genuinely pauses the generative state,
        // not just playback -- whatever's already ringing in activeNotes/
        // pendingNotes still decays normally below, untouched.
        if (!freeze && evaluateTrigCondition(track, loopIndex) && evaluateStepTrigger(track, stepIndex, rotationDriftSteps))
        {
            applyHarmonyDrift(track);
            int scaleRoot = scaleDegreeToNote(globalParams.key, globalParams.scaleMode, track.scaleDegree, track.rootNote);
            NoteSet notes = buildTrackNotes(track, scaleRoot, globalParams.key, globalParams.scaleMode);

            if (globalParams.globalTranspose != 0)
            {
                for (int& n : notes) n = foldIntoMidiRange(n + globalParams.globalTranspose);
                std::sort(notes.begin(), notes.end());
                notes.count = (int)(std::unique(notes.begin(), notes.end()) - notes.begin());
            }

            // Global polyphony cap. Voices (chordNotes) already limits a single
            // hit's chord to at most 6 notes, but with long noteLengthSteps
            // several hits' chords stack on top of each other while they're
            // all still ringing -- that's the actual source of more voices
            // than the Voices knob would suggest. Make room oldest-first
            // (activeNotes before never-yet-triggered pendingNotes) rather
            // than just dropping the new hit, so the sequence still feels
            // continuous instead of periodically going silent.
            if (notes.count > maxVoices)
                notes.count = maxVoices;

            int totalVoices = activeNotes.count + pendingNotes.count;
            while (totalVoices + notes.count > maxVoices && !activeNotes.empty())
            {
                IMidiMsg offMsg;
                offMsg.MakeNoteOffMsg(activeNotes.front().noteNumber, 0, activeNotes.front().channel - 1);
                SendMidiMsg(offMsg);
                activeNotes.removeAt(0);
                --totalVoices;
            }
            while (totalVoices + notes.count > maxVoices && !pendingNotes.empty())
            {
                pendingNotes.removeAt(0); // never got a note-on, just drop it
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
            humanizedVelocity = applyAccent(humanizedVelocity, stepIndex, track.accentEvery, track.accentAmount);
            int velocity = std::max(1, static_cast<int>(humanizedVelocity * 127.0f));
            int timingOffset = computeTimingOffsetSamples(track, current16thNote, globalParams, sixteenthNoteSamples);

            // Ratchet: only for hits that resolve to a single note -- see
            // EuclideanTrack::ratchetCount. Spreads retriggers across up to a
            // full beat (4 steps) rather than cramming them into a single
            // 16th note -- confining to one step meant even Ratchet=2
            // collapsed a multi-second sustained note into ~50ms blips, a
            // jarring cliff rather than a gradual increase in busyness.
            // Capped at the note's own length so a short, already-punchy
            // note isn't stretched wider than it was configured to be.
            int ratchetCount = std::clamp(track.ratchetCount, 1, 8);
            bool applyRatchet = ratchetCount > 1 && notes.count == 1;
            int ratchetSpanSteps = std::min(std::max(1, track.noteLengthSteps), 4);
            int ratchetSpanSamples = sixteenthNoteSamples * ratchetSpanSteps;
            int ratchetSliceSamples = std::max(1, ratchetSpanSamples / ratchetCount);
            int ratchetGateSamples = std::max(1, (int)(ratchetSliceSamples * track.gate));

            SendModCCs();

            for (int note : notes)
            {
                // Guard against a stuck note if this pitch is still sounding
                // (or queued) from an earlier hit -- see KillExistingNote's comment.
                // Called once here, before scheduling this hit's note(s)/ratchet
                // burst -- NOT between individual ratchet retriggers below, which
                // are deliberately sequential and must not cancel each other.
                KillExistingNote(note, track.midiChannel);

                if (!applyRatchet)
                {
                    if (timingOffset <= 0)
                    {
                        IMidiMsg msg;
                        msg.MakeNoteOnMsg(note, velocity, 0, track.midiChannel - 1);
                        SendMidiMsg(msg);
                        activeNotes.push({ note, track.midiChannel, gateSamples });
                    }
                    else
                    {
                        pendingNotes.push({ note, track.midiChannel, velocity, timingOffset, gateSamples });
                    }
                }
                else
                {
                    for (int r = 0; r < ratchetCount; ++r)
                    {
                        int onset = timingOffset + r * ratchetSliceSamples;
                        if (r == 0 && onset <= 0)
                        {
                            IMidiMsg msg;
                            msg.MakeNoteOnMsg(note, velocity, 0, track.midiChannel - 1);
                            SendMidiMsg(msg);
                            activeNotes.push({ note, track.midiChannel, ratchetGateSamples });
                        }
                        else
                        {
                            pendingNotes.push({ note, track.midiChannel, velocity, onset, ratchetGateSamples });
                        }
                    }
                }
            }
        }
    }
}

WDL_String MidiGenerator::ExportPatternAsMidiFile()
{
    int numBars = GetParam(kParamExportBars)->Int();
    int variations = std::clamp(GetParam(kParamExportVariations)->Int(), 1, 8);
    double bpm = GetTempo();
    if (bpm <= 0.0) bpm = 120.0;

    const char* tmpDir = std::getenv("TMPDIR");
    if (!tmpDir || !*tmpDir) tmpDir = "/tmp";

    // Reveals the FIRST file in Finder (see OnMessage) -- since all
    // variations land in the same folder, that shows every file at once,
    // no need to reveal each individually.
    WDL_String firstPath;

    for (int v = 0; v < variations; ++v)
    {
        EuclideanTrack trackCopy = track;

        // Variation 0 always exports exactly the live track's current
        // state -- unreseeded -- so "1 variation" (the default) is byte-
        // for-byte the same export this always produced, untouched by this
        // feature. Only variations beyond the first get a fresh reseed
        // (same as Reroll), applied to this offline copy only -- the
        // actually-playing track is never touched by export.
        if (v > 0)
            ReseedTrack(trackCopy);

        auto bytes = MidiExport::renderPatternToSMF(trackCopy, globalParams, bpm, numBars, modSlots, 8, maxVoices);

        WDL_String path;
        if (variations == 1)
            path.SetFormatted(1024, "%s/MidiGenerator_Export.mid", tmpDir);
        else
            path.SetFormatted(1024, "%s/MidiGenerator_Export_%d.mid", tmpDir, v + 1);

        if (!MidiExport::writeSMFFile(bytes, path.Get()))
            return WDL_String(); // bail on the first failure -- a partial batch isn't useful

        if (v == 0) firstPath = path;
    }

    return firstPath;
}
