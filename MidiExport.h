#pragma once

#include "MidiGeneratorLogic.h"
#include <vector>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <cmath>

//==============================================================================
// Standard MIDI File (format 0) export.
//
// Renders a fixed number of bars of the *current* pattern settings offline --
// deterministically re-running the same functions ProcessBlock uses live
// (evaluateStepTrigger / applyHarmonyDrift / buildChordInKey / ...) against a
// tick clock instead of a sample clock -- and writes the result as a .mid
// file. The intended use is IGraphics::InitiateExternalFileDragDrop(): drag
// the exported file straight onto a DAW's arrange/piano-roll view (e.g.
// Logic Pro) to drop it in as a real, editable MIDI region.
//
// Pure C++, no iPlug2/IGraphics dependency beyond MidiGeneratorLogic.h, so
// it's testable standalone.
//==============================================================================
namespace MidiExport
{

constexpr int kTicksPerQuarter = 480;
constexpr int kStepsPerBar     = 16; // 4/4, 16th-note grid -- matches ProcessBlock's step mapping

inline void writeVarLen(std::vector<uint8_t>& buf, uint32_t value)
{
    uint8_t bytes[4];
    int n = 0;
    bytes[n++] = (uint8_t)(value & 0x7F);
    value >>= 7;
    while (value > 0)
    {
        bytes[n++] = (uint8_t)((value & 0x7F) | 0x80);
        value >>= 7;
    }
    for (int i = n - 1; i >= 0; --i)
        buf.push_back(bytes[i]);
}

inline void writeU16BE(std::vector<uint8_t>& buf, uint16_t v)
{
    buf.push_back((uint8_t)((v >> 8) & 0xFF));
    buf.push_back((uint8_t)(v & 0xFF));
}

inline void writeU32BE(std::vector<uint8_t>& buf, uint32_t v)
{
    buf.push_back((uint8_t)((v >> 24) & 0xFF));
    buf.push_back((uint8_t)((v >> 16) & 0xFF));
    buf.push_back((uint8_t)((v >> 8) & 0xFF));
    buf.push_back((uint8_t)(v & 0xFF));
}

struct RawEvent
{
    uint32_t tick;
    int priority; // tie-break at equal tick: CC(0) < NoteOff(1) < NoteOn(2) -- avoids stuck notes
    std::vector<uint8_t> bytes;
};

// A currently-"sounding" note in the offline simulation below, tracking the
// index of its (already-queued) note-off RawEvent so a later voice-steal (or
// a same-pitch retrigger) can truncate that event's tick in place -- mirrors
// ProcessBlock's activeNotes list, just offline. noteNumber lets us find the
// still-sounding instance of a pitch about to be re-triggered, which is the
// offline equivalent of ProcessBlock's KillExistingNote().
struct TrackedNote
{
    uint32_t offTick;
    size_t offEventIndex;
    int noteNumber;
};

// Renders `numBars` bars of `track`'s pattern under `globalParams`/`bpm` as a
// Standard MIDI File (format 0, single track). Takes `track` BY VALUE: the
// simulation calls the same live functions ProcessBlock does, which mutate
// rngState/scaleDegree/chaosX, and that must stay local to this render --
// exporting a preview must not perturb the actually-playing instance.
inline std::vector<uint8_t> renderPatternToSMF(EuclideanTrack track, const GlobalParams& globalParams,
                                                double bpm, int numBars, const ModSlot* modSlots, int numModSlots,
                                                int maxVoices = 8)
{
    const int sixteenthNoteTicks = kTicksPerQuarter / 4;
    const int totalSteps = std::max(1, numBars) * kStepsPerBar;
    const int patternSteps = std::max(1, track.steps);
    const int channel = (track.midiChannel - 1) & 0x0F;
    maxVoices = std::max(1, maxVoices);

    std::vector<RawEvent> events;
    std::vector<TrackedNote> tracked; // mirrors ProcessBlock's activeNotes, oldest-first

    for (int step = 0; step < totalSteps; ++step)
    {
        int stepIndex = step % patternSteps;
        int loopIndex = step / patternSteps;
        int stepStartTick = step * sixteenthNoteTicks;
        int rotationDriftSteps = track.rotationDriftPeriod > 0 ? (step / track.rotationDriftPeriod) : 0;

        if (!evaluateTrigCondition(track, loopIndex) || !evaluateStepTrigger(track, stepIndex, rotationDriftSteps))
            continue;

        applyHarmonyDrift(track);
        int scaleRoot = scaleDegreeToNote(globalParams.key, globalParams.scaleMode, track.scaleDegree, track.rootNote);
        NoteSet notes = buildTrackNotes(track, scaleRoot, globalParams.key, globalParams.scaleMode);

        if (globalParams.globalTranspose != 0)
        {
            for (int& n : notes) n = foldIntoMidiRange(n + globalParams.globalTranspose);
            std::sort(notes.begin(), notes.end());
            notes.count = (int)(std::unique(notes.begin(), notes.end()) - notes.begin());
        }

        int gateTicks = std::max(1, (int)(sixteenthNoteTicks * std::max(1, track.noteLengthSteps) * track.gate));
        // Mirrors ProcessBlock's downward-only humanize -- see the comment
        // there. Uses the track's own RNG so the exported file's velocity
        // pattern is generated the same way live playback's is.
        float humanizedVelocity = track.velocity * (1.0f - randUnit01(track.rngState) * 0.2f);
        humanizedVelocity = applyAccent(humanizedVelocity, stepIndex, track.accentEvery, track.accentAmount);
        int velocity = std::min(127, std::max(1, (int)(humanizedVelocity * 127.0f)));
        int timingOffsetTicks = computeTimingOffsetSamples(track, step, globalParams, sixteenthNoteTicks);

        int onTick = stepStartTick + timingOffsetTicks;
        int offTick = onTick + gateTicks;

        // Ratchet: mirrors ProcessBlock exactly (ticks instead of samples) --
        // see EuclideanTrack::ratchetCount and the comment in ProcessBlock.
        int ratchetCount = std::clamp(track.ratchetCount, 1, 8);
        bool applyRatchet = ratchetCount > 1 && notes.count == 1;
        int ratchetSpanSteps = std::min(std::max(1, track.noteLengthSteps), 4);
        int ratchetSpanTicks = sixteenthNoteTicks * ratchetSpanSteps;
        int ratchetSliceTicks = std::max(1, ratchetSpanTicks / ratchetCount);
        int ratchetGateTicks = std::max(1, (int)(ratchetSliceTicks * track.gate));

        for (int mi = 0; mi < numModSlots; ++mi)
        {
            const ModSlot& slot = modSlots[mi];
            if (slot.source == ModSource::Off) continue;
            float value = evaluateModSource(track, slot.source);
            uint8_t data2 = (uint8_t)std::min(127, std::max(0, (int)(value * 127.0f)));
            events.push_back({ (uint32_t)stepStartTick, 0,
                { (uint8_t)(0xB0 | channel), (uint8_t)(slot.ccNumber & 0x7F), data2 } });
        }

        // Global polyphony cap, mirroring ProcessBlock exactly: expire
        // naturally-ended notes, trim an oversized chord to maxVoices, then
        // steal the oldest still-sounding notes (truncating their already-
        // queued note-off to right now) if there still isn't room.
        tracked.erase(std::remove_if(tracked.begin(), tracked.end(),
            [onTick](const TrackedNote& t) { return t.offTick <= (uint32_t)onTick; }), tracked.end());

        if (notes.count > maxVoices)
            notes.count = maxVoices;

        while ((int)tracked.size() + notes.count > maxVoices && !tracked.empty())
        {
            events[tracked.front().offEventIndex].tick = (uint32_t)onTick;
            tracked.erase(tracked.begin());
        }

        // Emits one note-on/off pair for pitch `n` starting at `on`, first
        // truncating any still-sounding earlier instance of the same pitch to
        // `on` -- the offline equivalent of ProcessBlock's KillExistingNote,
        // so the exported file never stacks two overlapping note-ons of the
        // same pitch (a DAW would import that as messy duplicate notes). With
        // the default 32-step note length, successive hits almost always
        // overlap, and repeated/shared pitches are common, so without this the
        // export routinely diverges from what the live engine plays.
        auto emitNote = [&](uint8_t n, uint32_t on, uint32_t off)
        {
            for (auto it = tracked.begin(); it != tracked.end(); )
            {
                if (it->noteNumber == n && it->offTick > on)
                {
                    events[it->offEventIndex].tick = on; // release the old instance exactly as the new one starts
                    it = tracked.erase(it);
                }
                else ++it;
            }

            events.push_back({ on, 2, { (uint8_t)(0x90 | channel), n, (uint8_t)velocity } });
            size_t offIdx = events.size();
            events.push_back({ off, 1, { (uint8_t)(0x80 | channel), n, 0 } });
            tracked.push_back({ off, offIdx, (int)n });
        };

        for (int note : notes)
        {
            uint8_t n = (uint8_t)std::min(127, std::max(0, note));

            if (!applyRatchet)
            {
                emitNote(n, (uint32_t)onTick, (uint32_t)offTick);
            }
            else
            {
                for (int r = 0; r < ratchetCount; ++r)
                {
                    uint32_t rOnTick = (uint32_t)(onTick + r * ratchetSliceTicks);
                    uint32_t rOffTick = rOnTick + (uint32_t)ratchetGateTicks;
                    emitNote(n, rOnTick, rOffTick);
                }
            }
        }
    }

    std::stable_sort(events.begin(), events.end(), [](const RawEvent& a, const RawEvent& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        return a.priority < b.priority;
    });

    // --- Track chunk body ---
    std::vector<uint8_t> trackBytes;

    uint32_t microsPerQuarter = (uint32_t)std::lround(60000000.0 / std::max(1.0, bpm));
    writeVarLen(trackBytes, 0);
    trackBytes.push_back(0xFF); trackBytes.push_back(0x51); trackBytes.push_back(0x03);
    trackBytes.push_back((uint8_t)((microsPerQuarter >> 16) & 0xFF));
    trackBytes.push_back((uint8_t)((microsPerQuarter >> 8) & 0xFF));
    trackBytes.push_back((uint8_t)(microsPerQuarter & 0xFF));

    uint32_t prevTick = 0;
    for (const auto& ev : events)
    {
        writeVarLen(trackBytes, ev.tick - prevTick);
        for (uint8_t b : ev.bytes) trackBytes.push_back(b);
        prevTick = ev.tick;
    }

    uint32_t endTick = (uint32_t)totalSteps * (uint32_t)sixteenthNoteTicks;
    writeVarLen(trackBytes, endTick > prevTick ? endTick - prevTick : 0);
    trackBytes.push_back(0xFF); trackBytes.push_back(0x2F); trackBytes.push_back(0x00); // End of Track

    // --- Assemble file: MThd + MTrk ---
    std::vector<uint8_t> file;
    file.push_back('M'); file.push_back('T'); file.push_back('h'); file.push_back('d');
    writeU32BE(file, 6);
    writeU16BE(file, 0); // format 0
    writeU16BE(file, 1); // 1 track
    writeU16BE(file, (uint16_t)kTicksPerQuarter);

    file.push_back('M'); file.push_back('T'); file.push_back('r'); file.push_back('k');
    writeU32BE(file, (uint32_t)trackBytes.size());
    file.insert(file.end(), trackBytes.begin(), trackBytes.end());

    return file;
}

inline bool writeSMFFile(const std::vector<uint8_t>& bytes, const char* path)
{
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    size_t written = fwrite(bytes.data(), 1, bytes.size(), f);
    fclose(f);
    return written == bytes.size();
}

} // namespace MidiExport
