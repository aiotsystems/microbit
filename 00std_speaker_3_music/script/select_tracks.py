"""Pick the tracks to play on the micro:bit boards, one track per board.

Usage: python3 select_tracks.py [songs/] [count]

Reads the songs/song_*.h headers, lists each track with a few statistics, and
lets you tick up to <count> tracks (15 by default). You can listen to one track,
or to all the ticked ones together, as the boards will play them (square waves,
close to what the micro:bit speaker plays); tick and untick while it plays. The selection is saved in
selection.json, next to songs/, and reloaded on the next run.
"""
import json, os, re, subprocess, sys, tempfile, time, wave
import numpy as np
import tkinter as tk
if sys.platform == 'win32':
    import winsound
from tkinter import ttk, messagebox

PWM_CLOCK_HZ = 4_000_000   # see csv_to_song.py
SAMPLE_RATE  = 22050
NOTES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']

def note_name(freq):
    import math
    midi = round(69 + 12 * math.log2(freq / 440))
    return f"{NOTES[midi % 12]}{midi // 12 - 1}"

def read_tracks(songs_dir):
    """Return [{index, name, notes: [(period, duration_ms)], ...statistics}], sorted by index.

    index is the track number on the boards: its place in tracks[], in songs.h."""
    entries = re.findall(r'\[\s*(\d+)\] = TRACK\((\w+), "([^"]*)"\)',
                         open(os.path.join(songs_dir, 'songs.h')).read())
    tracks = []
    for index, array, name in entries:
        text = open(os.path.join(songs_dir, array + '.h')).read()
        notes = [(int(p), int(d)) for p, d in re.findall(r'\{\s*(\d+),\s*(\d+)\s*\}', text)]
        played = [(p, d) for p, d in notes if p]
        freqs = [PWM_CLOCK_HZ / p for p, _ in played]
        total = sum(d for _, d in notes)
        tracks.append({
            'index':    int(index),
            'name':     name,
            'notes':    notes,
            'count':    len(played),
            'pitches':  len({p for p, _ in played}),
            'duration': total / 1000,
            'sounding': 100 * sum(d for _, d in played) / total if total else 0,
            'range':    f"{note_name(min(freqs))}-{note_name(max(freqs))}" if freqs else '',
            'lowest':   min(freqs) if freqs else 0,
        })
    return sorted(tracks, key=lambda t: t['index'])

def render(notes):
    """The notes as a square wave (-1, +1, 0 for silence), like the PWM plays them."""
    parts = []
    for period, duration in notes:
        n = SAMPLE_RATE * duration // 1000
        if period == 0:
            parts.append(np.zeros(n, np.float32))
            continue
        half = SAMPLE_RATE * period / PWM_CLOCK_HZ / 2   # samples per half period
        parts.append(np.where((np.arange(n) / half) % 2 < 1, 1, -1).astype(np.float32))
    return np.concatenate(parts) if parts else np.zeros(0, np.float32)

class Player:
    """Plays a mix of tracks in the background; the mix can change while it plays."""
    def __init__(self):
        self.process = None
        self.paths = [os.path.join(tempfile.gettempdir(), f'microbit_tracks_{i}.wav') for i in (0, 1)]
        self.started = None   # time.monotonic() at position 0 of what is playing
        self.length = 0       # length of what is playing, in s

    def position(self):
        """Where we are in the song, in s, or None if nothing plays."""
        if self.started is None or time.monotonic() - self.started >= self.length:
            return None
        return time.monotonic() - self.started

    def play(self, waves, start=0.0):
        """Play the sum of the waves (numpy arrays), from <start> s."""
        self.stop()
        waves = [w for w in waves if len(w)]
        if not waves:
            return
        mix = np.zeros(max(len(w) for w in waves), np.float32)
        for w in waves:
            mix[:len(w)] += w
        mix *= 0.6 / len(waves)
        first = int(start * SAMPLE_RATE)
        if first >= len(mix):
            return
        # alternate between two files: the one just stopped may still be open
        self.paths.reverse()
        with wave.open(self.paths[0], 'wb') as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(SAMPLE_RATE)
            w.writeframes((mix[first:] * 32767).astype('<i2').tobytes())
        if sys.platform == 'win32':
            winsound.PlaySound(self.paths[0], winsound.SND_FILENAME | winsound.SND_ASYNC)
        else:
            self.process = subprocess.Popen(['afplay' if sys.platform == 'darwin' else 'aplay', self.paths[0]])
        self.started = time.monotonic() - start
        self.length = len(mix) / SAMPLE_RATE

    def stop(self):
        if sys.platform == 'win32':
            winsound.PlaySound(None, 0)
        elif self.process:
            self.process.terminate()
            self.process = None
        self.started = None

class App:
    COLUMNS = [  # (key, heading, width, anchor)
        ('selected', '✓',          30, 'center'),
        ('index',    'Track',      50, 'e'),
        ('name',     'Instrument', 160, 'w'),
        ('count',    'Notes',      60, 'e'),
        ('pitches',  'Pitches',    60, 'e'),
        ('range',    'Range',      80, 'center'),
        ('duration', 'Length (s)', 75, 'e'),
        ('sounding', 'Sounding',   70, 'e'),
    ]

    def __init__(self, root, tracks, count, selection_path):
        self.root, self.tracks, self.count, self.selection_path = root, tracks, count, selection_path
        self.player = Player()
        self.waves = {}         # track index -> rendered square wave, see wave()
        self.solo = None        # track index playing alone, None when playing the selection
        self.selected = set()
        if os.path.exists(selection_path):
            self.selected = set(json.load(open(selection_path))['tracks'])
        root.title('Music: pick the tracks for the micro:bit boards')

        self.tree = ttk.Treeview(root, columns=[c[0] for c in self.COLUMNS], show='headings',
                                 height=len(tracks), selectmode='browse')
        for key, heading, width, anchor in self.COLUMNS:
            self.tree.heading(key, text=heading, command=lambda k=key: self.sort(k))
            self.tree.column(key, width=width, anchor=anchor, stretch=key == 'name')
        self.tree.tag_configure('selected', background='#d8f0d8')
        self.tree.bind('<Button-1>', self.on_click)
        self.tree.bind('<space>', lambda e: self.toggle(self.tree.focus()))
        self.tree.bind('<Return>', lambda e: self.play_solo())
        self.tree.grid(row=0, column=0, columnspan=6, sticky='nsew', padx=8, pady=8)
        self.tree.focus_set()

        ttk.Button(root, takefocus=False, text='▶ Play selection', command=self.play_selection).grid(row=1, column=0, padx=8, pady=(0, 8), sticky='w')
        ttk.Button(root, takefocus=False, text='▶ Solo', command=self.play_solo).grid(row=1, column=1, pady=(0, 8), sticky='w')
        ttk.Button(root, takefocus=False, text='■ Stop', command=self.player.stop).grid(row=1, column=2, padx=8, pady=(0, 8), sticky='w')
        self.status = ttk.Label(root)
        self.status.grid(row=1, column=3, sticky='w', padx=8, pady=(0, 8))
        ttk.Button(root, takefocus=False, text='Clear', command=self.clear).grid(row=1, column=4, pady=(0, 8), sticky='e')
        ttk.Button(root, takefocus=False, text='Save', command=self.save).grid(row=1, column=5, padx=8, pady=(0, 8), sticky='e')
        root.columnconfigure(3, weight=1)
        root.rowconfigure(0, weight=1)
        root.protocol('WM_DELETE_WINDOW', self.quit)

        self.order = list(range(len(tracks)))
        self.sort_key, self.reverse = 'index', False
        self.refresh()

    def values(self, t):
        return ('☑' if t['index'] in self.selected else '☐', t['index'], t['name'], t['count'],
                t['pitches'], t['range'], f"{t['duration']:.1f}", f"{t['sounding']:.0f} %")

    def refresh(self):
        focus = self.tree.focus()
        self.tree.delete(*self.tree.get_children())
        for i in self.order:
            t = self.tracks[i]
            self.tree.insert('', 'end', iid=str(i), values=self.values(t),
                             tags=('selected',) if t['index'] in self.selected else ())
        if focus:
            self.tree.focus(focus); self.tree.selection_set(focus)
        n = len(self.selected)
        self.status.config(text=f"{n} / {self.count} selected  ·  click ✓ or press space to (un)select, "
                                f"double-click or Enter to listen to one track",
                           foreground='green' if n == self.count else 'black')

    def sort(self, key):
        self.reverse = not self.reverse if key == self.sort_key else key in ('count', 'pitches', 'duration', 'sounding')
        self.sort_key = key
        sort_by = {'selected': lambda t: t['index'] in self.selected, 'range': lambda t: t['lowest']}
        self.order.sort(key=lambda i: sort_by.get(key, lambda t: t[key])(self.tracks[i]), reverse=self.reverse)
        self.refresh()

    def on_click(self, event):
        row = self.tree.identify_row(event.y)
        if not row or self.tree.identify_region(event.x, event.y) != 'cell':
            return
        self.tree.focus_set()
        if self.tree.identify_column(event.x) == '#1':
            self.toggle(row)
            return 'break'

    def toggle(self, row):
        if not row:
            return
        index = self.tracks[int(row)]['index']
        if index in self.selected:
            self.selected.remove(index)
        elif len(self.selected) >= self.count:
            self.root.bell()
            return
        else:
            self.selected.add(index)
        self.tree.focus(row); self.tree.selection_set(row)
        self.refresh()
        self.selection_changed()

    def wave(self, index):
        if index not in self.waves:
            self.waves[index] = render(next(t for t in self.tracks if t['index'] == index)['notes'])
        return self.waves[index]

    def play_selection(self, start=0.0):
        """Play all the selected tracks together, as the boards will."""
        self.solo = None
        self.player.play([self.wave(i) for i in sorted(self.selected)], start)

    def play_solo(self):
        row = self.tree.focus()
        if row:
            self.solo = self.tracks[int(row)]['index']
            self.player.play([self.wave(self.solo)])

    def selection_changed(self):
        """While the selection plays, apply the change right away, without going back to the start."""
        position = self.player.position()
        if position is not None and self.solo is None:
            self.play_selection(position)

    def clear(self):
        self.selected.clear()
        self.refresh()
        self.selection_changed()

    def save(self):
        chosen = [t for t in self.tracks if t['index'] in self.selected]
        with open(self.selection_path, 'w') as f:
            json.dump({'tracks': [t['index'] for t in chosen],
                       'names':  [t['name'] for t in chosen]}, f, indent=2)
        print(f"saved {len(chosen)} tracks in {self.selection_path}:")
        for board, t in enumerate(chosen):
            print(f"  board {board:2d}: track {t['index']:2d} {t['name']}")
        messagebox.showinfo('Saved', f"{len(chosen)} / {self.count} tracks saved in\n{self.selection_path}")

    def quit(self):
        self.player.stop()
        self.root.destroy()

if __name__ == '__main__':
    here = os.path.dirname(os.path.abspath(__file__))
    songs_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, '..', 'songs')
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 15
    selection_path = os.path.join(os.path.dirname(os.path.abspath(songs_dir)), 'selection.json')
    root = tk.Tk()
    app = App(root, read_tracks(songs_dir), count, selection_path)
    app.tree.bind('<Double-1>', lambda e: app.tree.identify_column(e.x) != '#1' and app.play_solo())
    root.mainloop()
