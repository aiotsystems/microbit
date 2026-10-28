"""Convert each track of a MIDI file into a monophonic CSV (note,frequency_hz,duration_ms).

Usage: python3 midi_to_csv.py song.mid out_dir/

One CSV per track that plays notes, named <midi name>_<track index>_<track name>.csv.
The speaker plays one note at a time, so each track is reduced to a melody:
  - notes starting together (a chord) are reduced to the longest one, then the
    highest: the held note carries the melody, short ones are ornaments
  - a note is cut when the next one starts
  - gaps between notes become "silence" lines
Times are rounded to the millisecond on the absolute timeline, so durations
do not drift over the song.
"""
import os, re, sys

NOTES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']

def vlq(data, i):
    val = 0
    while True:
        b = data[i]; i += 1
        val = (val << 7) | (b & 0x7F)
        if not b & 0x80:
            return val, i

def read_midi(path):
    """Return (ticks per quarter note, tempo changes [(tick, us/quarter)], tracks [(name, [(start, end, pitch)])])."""
    data = open(path, 'rb').read()
    assert data[:4] == b'MThd', 'not a MIDI file'
    hlen = int.from_bytes(data[4:8], 'big')
    ntrks = int.from_bytes(data[10:12], 'big')
    division = int.from_bytes(data[12:14], 'big')
    assert not division & 0x8000, 'SMPTE time division not supported'
    tempos, tracks = [], []
    pos = 8 + hlen
    for _ in range(ntrks):
        assert data[pos:pos+4] == b'MTrk', 'invalid track'
        i, end = pos + 8, pos + 8 + int.from_bytes(data[pos+4:pos+8], 'big')
        tick, status, name, on, notes = 0, 0, None, {}, []
        while i < end:
            dt, i = vlq(data, i); tick += dt
            if data[i] & 0x80:
                status = data[i]; i += 1
            if status == 0xFF:
                typ = data[i]; i += 1
                ln, i = vlq(data, i)
                payload = data[i:i+ln]; i += ln
                if typ == 0x03 and name is None: name = payload.decode('latin-1')
                elif typ == 0x51: tempos.append((tick, int.from_bytes(payload, 'big')))
                elif typ == 0x2F: break
            elif status in (0xF0, 0xF7):
                ln, i = vlq(data, i); i += ln
            else:
                kind = status & 0xF0
                n = 1 if kind in (0xC0, 0xD0) else 2
                args = data[i:i+n]; i += n
                if kind == 0x90 and args[1] > 0:
                    on.setdefault(args[0], []).append(tick)
                elif kind == 0x80 or kind == 0x90:
                    if on.get(args[0]):
                        notes.append((on[args[0]].pop(0), tick, args[0]))
        tracks.append((name or '', sorted(notes)))
        pos = end
    return division, sorted(tempos) or [(0, 500000)], tracks

def tick_to_ms(division, tempos):
    """Build a tick -> ms function from the tempo map."""
    def conv(tick):
        ms, last_tick, us = 0.0, 0, 500000
        for t, new_us in tempos:
            if t >= tick: break
            ms += (t - last_tick) * us / division / 1000
            last_tick, us = t, new_us
        return ms + (tick - last_tick) * us / division / 1000
    return conv

def melody(notes):
    """Reduce a polyphonic track to [(start, end, pitch)], one note at a time."""
    kept = {}
    for start, end, pitch in notes:
        if start not in kept or (end, pitch) > kept[start]:
            kept[start] = (end, pitch)
    starts = sorted(kept)
    out = []
    for k, start in enumerate(starts):
        end, pitch = kept[start]
        if k + 1 < len(starts):
            end = min(end, starts[k+1])
        out.append((start, end, pitch))
    return out

def to_rows(notes, to_ms):
    rows, now = [], 0
    for start, end, pitch in melody(notes):
        t0, t1 = round(to_ms(start)), round(to_ms(end))
        if t1 <= t0:
            continue  # shorter than 1 ms once rounded: absorbed by the next silence
        if t0 > now:
            rows.append(('silence', 0, t0 - now))
        rows.append((f"{NOTES[pitch % 12]}{pitch // 12 - 1}", round(440 * 2 ** ((pitch - 69) / 12), 2), t1 - t0))
        now = t1
    return rows

if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    source, out_dir = sys.argv[1:]
    division, tempos, tracks = read_midi(source)
    to_ms = tick_to_ms(division, tempos)
    base = os.path.splitext(os.path.basename(source))[0]
    os.makedirs(out_dir, exist_ok=True)
    for index, (name, notes) in enumerate(tracks):
        if not notes:
            continue
        clean = re.sub(r'[^A-Za-z0-9]+', '_', name).strip('_')
        path = os.path.join(out_dir, f"{base}_{index:02d}_{clean}.csv")
        with open(path, 'w') as f:
            f.write('note,frequency_hz,duration_ms\n')
            for row in to_rows(notes, to_ms):
                f.write('%s,%s,%s\n' % row)
        print(path)
