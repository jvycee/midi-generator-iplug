#!/usr/bin/python3

# Standalone Standard MIDI File (SMF) dumper for eyeballing/verifying exported
# .mid files -- e.g. Drift's "Drag Generated MIDI" output, or the same file
# after it's been dragged into a DAW and bounced back out. No dependencies:
# a self-contained SMF reader (mirrors what MidiExport.h writes, but reads
# ANY format-0/1 file, multi-track, running status included).
#
# Usage:
#   scripts/dump_midi.py path/to/file.mid [file2.mid ...]
#   scripts/dump_midi.py --events path/to/file.mid   # also list CC/tempo/meta

import sys
import argparse

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def note_name(n):
    # Scientific pitch notation, middle C (60) = C4.
    return f"{NOTE_NAMES[n % 12]}{n // 12 - 1}"


def read_varlen(data, pos):
    value = 0
    while True:
        b = data[pos]
        pos += 1
        value = (value << 7) | (b & 0x7F)
        if not (b & 0x80):
            break
    return value, pos


def parse_smf(data):
    if data[0:4] != b"MThd":
        raise ValueError("not a MIDI file (missing MThd)")
    header_len = int.from_bytes(data[4:8], "big")
    fmt = int.from_bytes(data[8:10], "big")
    ntrks = int.from_bytes(data[10:12], "big")
    division = int.from_bytes(data[12:14], "big")
    if division & 0x8000:
        raise ValueError("SMPTE time division not supported")
    ticks_per_quarter = division

    pos = 8 + header_len
    tracks = []
    for track_index in range(ntrks):
        if data[pos:pos + 4] != b"MTrk":
            raise ValueError(f"expected MTrk at byte {pos}, found {data[pos:pos + 4]!r}")
        track_len = int.from_bytes(data[pos + 4:pos + 8], "big")
        track_end = pos + 8 + track_len
        pos += 8

        events = []
        tick = 0
        running_status = None
        while pos < track_end:
            delta, pos = read_varlen(data, pos)
            tick += delta
            status = data[pos]
            if status & 0x80:
                running_status = status
                pos += 1
            else:
                status = running_status  # running status: reuse last, don't consume a byte

            if status == 0xFF:  # meta event
                meta_type = data[pos]
                pos += 1
                length, pos = read_varlen(data, pos)
                payload = data[pos:pos + length]
                pos += length
                events.append({"tick": tick, "kind": "meta", "type": meta_type, "data": payload})
            elif status in (0xF0, 0xF7):  # sysex
                length, pos = read_varlen(data, pos)
                pos += length
                events.append({"tick": tick, "kind": "sysex"})
            else:
                hi = status & 0xF0
                channel = status & 0x0F
                if hi in (0xC0, 0xD0):  # program change, channel pressure: 1 data byte
                    d1 = data[pos]
                    pos += 1
                    events.append({"tick": tick, "kind": "other", "status": hi, "channel": channel, "d1": d1})
                else:  # note on/off, poly pressure, CC, pitch bend: 2 data bytes
                    d1 = data[pos]
                    d2 = data[pos + 1]
                    pos += 2
                    if hi == 0x90 and d2 > 0:
                        events.append({"tick": tick, "kind": "note_on", "channel": channel, "note": d1, "velocity": d2})
                    elif hi == 0x80 or (hi == 0x90 and d2 == 0):
                        events.append({"tick": tick, "kind": "note_off", "channel": channel, "note": d1})
                    elif hi == 0xB0:
                        events.append({"tick": tick, "kind": "cc", "channel": channel, "cc": d1, "value": d2})
                    else:
                        events.append({"tick": tick, "kind": "other", "status": hi, "channel": channel, "d1": d1, "d2": d2})
        tracks.append(events)
        pos = track_end

    return ticks_per_quarter, fmt, tracks


def tempo_from_meta(payload):
    micros_per_quarter = int.from_bytes(payload, "big")
    return 60000000.0 / micros_per_quarter


def bar_beat_tick(tick, ticks_per_quarter):
    ticks_per_bar = ticks_per_quarter * 4  # assumes 4/4, matching Drift's grid
    bar = tick // ticks_per_bar + 1
    rem = tick % ticks_per_bar
    beat = rem // ticks_per_quarter + 1
    sub = rem % ticks_per_quarter
    return f"{bar}:{beat}:{sub:03d}"


def match_notes(events, ticks_per_quarter):
    """Pairs note_on/note_off per (channel, note) in FIFO order, flagging anomalies."""
    notes = []
    open_notes = {}  # (channel, note) -> list of dicts still sounding, oldest first
    anomalies = []

    for ev in events:
        key = (ev.get("channel"), ev.get("note"))
        if ev["kind"] == "note_on":
            n = {"on": ev["tick"], "off": None, "note": ev["note"], "velocity": ev["velocity"], "channel": ev["channel"]}
            if key in open_notes and open_notes[key]:
                anomalies.append(
                    f"  OVERLAP  {note_name(ev['note'])} ch{ev['channel']+1}: new note-on at "
                    f"{bar_beat_tick(ev['tick'], ticks_per_quarter)} while a previous one is still sounding"
                )
            notes.append(n)
            open_notes.setdefault(key, []).append(n)
        elif ev["kind"] == "note_off":
            pending = open_notes.get(key)
            if pending:
                n = pending.pop(0)
                n["off"] = ev["tick"]
                if n["off"] == n["on"]:
                    anomalies.append(
                        f"  ZERO-LEN {note_name(n['note'])} ch{n['channel']+1} at {bar_beat_tick(n['on'], ticks_per_quarter)}"
                    )
            else:
                anomalies.append(
                    f"  ORPHAN OFF  {note_name(ev['note'])} ch{ev['channel']+1} at "
                    f"{bar_beat_tick(ev['tick'], ticks_per_quarter)}: note-off with no matching note-on"
                )

    for key, pending in open_notes.items():
        for n in pending:
            anomalies.append(
                f"  STUCK  {note_name(n['note'])} ch{n['channel']+1}: note-on at "
                f"{bar_beat_tick(n['on'], ticks_per_quarter)} never got a note-off"
            )

    return notes, anomalies


def dump_file(path, show_events):
    with open(path, "rb") as f:
        data = f.read()

    ticks_per_quarter, fmt, tracks = parse_smf(data)
    print(f"=== {path} ===")
    print(f"format {fmt}, {len(tracks)} track(s), {ticks_per_quarter} ticks/quarter")

    for tempo_ev in (e for t in tracks for e in t if e["kind"] == "meta" and e["type"] == 0x51):
        print(f"tempo: {tempo_from_meta(tempo_ev['data']):.2f} BPM (at tick {tempo_ev['tick']})")

    for track_index, events in enumerate(tracks):
        notes, anomalies = match_notes(events, ticks_per_quarter)
        print(f"\n-- track {track_index}: {len(notes)} note(s) --")
        print(f"{'bar:beat:tick':<14} {'note':<5} {'vel':>4} {'dur(ticks)':>11} {'ch':>3}")
        for n in sorted(notes, key=lambda n: n["on"]):
            dur = (n["off"] - n["on"]) if n["off"] is not None else None
            dur_str = str(dur) if dur is not None else "OPEN"
            print(f"{bar_beat_tick(n['on'], ticks_per_quarter):<14} {note_name(n['note']):<5} "
                  f"{n['velocity']:>4} {dur_str:>11} {n['channel'] + 1:>3}")

        if anomalies:
            print(f"\n  ! {len(anomalies)} anomaly(ies):")
            for a in anomalies:
                print(a)
        else:
            print("  no anomalies detected")

        if show_events:
            print(f"\n  -- raw non-note events --")
            for ev in events:
                if ev["kind"] in ("note_on", "note_off"):
                    continue
                if ev["kind"] == "cc":
                    print(f"  {bar_beat_tick(ev['tick'], ticks_per_quarter):<14} CC{ev['cc']} = {ev['value']} (ch{ev['channel']+1})")
                elif ev["kind"] == "meta" and ev["type"] == 0x51:
                    continue  # already printed above
                elif ev["kind"] == "meta":
                    print(f"  {bar_beat_tick(ev['tick'], ticks_per_quarter):<14} meta 0x{ev['type']:02X} ({len(ev['data'])} bytes)")
                else:
                    print(f"  {bar_beat_tick(ev['tick'], ticks_per_quarter):<14} {ev['kind']}")
    print()


def main():
    parser = argparse.ArgumentParser(description="Dump/verify a Standard MIDI File's note content.")
    parser.add_argument("files", nargs="+", help="one or more .mid files")
    parser.add_argument("--events", action="store_true", help="also list raw CC/meta events per track")
    args = parser.parse_args()

    for path in args.files:
        dump_file(path, args.events)


if __name__ == "__main__":
    main()
