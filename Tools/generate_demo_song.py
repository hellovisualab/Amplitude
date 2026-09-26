#!/usr/bin/env python3
"""Generates "Neon Drive", the demo song bundled with Amplitude.

The song is synthesised from scratch (no samples), so it can be regenerated anywhere:

  * audio.wav  - 7-channel 16-bit 44.1 kHz multitrack WAV in the layout of spec 10.1.1
                 (0 drums, 1 synth, 2 bass, 3 vocals, 4 pad, 5 fx, 6 master/click reference)
  * song.json  - metadata + chart (spec 14.1). The chart is derived from the same musical events
                 that are rendered into the audio, so every note lines up with an instrument hit.
                 Every instrument gets its own track of gems in three columns (left / middle /
                 right, roughly low to high), so the player can hop to whichever lane they like.

Usage:
    python3 Tools/generate_demo_song.py                 # writes Songs/neon_drive/{song.json,audio.wav}
    python3 Tools/generate_demo_song.py --chart-only    # only rewrite song.json (fast)

Only the Python standard library is required.
"""

import argparse
import array
import json
import math
import os
import random
import sys
import wave

BPM = 130.0
BEAT = 60.0 / BPM          # seconds per beat
BAR = 4 * BEAT
BARS = 28
TAIL = 2.3                 # seconds of ring-out after the last bar
SAMPLE_RATE = 44100
LANES = ["drums", "synth", "bass", "vocals", "pad", "fx"]

# A minor: Am - F - C - G, one chord per bar.
PROGRESSION = [
    (57, 60, 64),  # A C E
    (53, 57, 60),  # F A C
    (48, 52, 55),  # C E G
    (55, 59, 62),  # G B D
]
BASS_ROOTS = [45, 41, 48, 43]  # A2 F2 C3 G2

# Sections by bar index.
INTRO = range(0, 2)
SECTION_A = range(2, 10)
SECTION_B = range(10, 18)
SECTION_C = range(18, 26)
OUTRO = range(26, 28)

LEFT, MIDDLE, RIGHT = 1, 2, 4  # column bits

# Chart tuning for the demo: a capture needs a full two-bar phrase (8 gems). Captured tracks then
# play by themselves for the default 30 s, so the song builds up as the player hops between them.
RULES = {"capture_streak": 8}


def midi_to_hz(note):
    return 440.0 * 2.0 ** ((note - 69) / 12.0)


def beat_time(bar, beat=0.0):
    return bar * BAR + beat * BEAT


class Track:
    """One mono instrument buffer with a few simple synthesis helpers."""

    def __init__(self, length_s, seed):
        self.samples = [0.0] * int(length_s * SAMPLE_RATE)
        self.rng = random.Random(seed)

    def tone(self, start, duration, freq, amp, wave_shape="sine", attack=0.004, decay=None, release=0.03,
             freq_end=None, vibrato=0.0, lowpass=None, harmonics=None):
        buf = self.samples
        first = int(start * SAMPLE_RATE)
        count = min(int(duration * SAMPLE_RATE), len(buf) - first)
        if count <= 0:
            return
        two_pi = 2.0 * math.pi
        sin = math.sin
        exp = math.exp
        phase = 0.0
        filtered = 0.0
        release_n = max(1, int(release * SAMPLE_RATE))
        attack_n = max(1, int(attack * SAMPLE_RATE))
        ratio = (freq_end / freq) if freq_end else 1.0
        inv_count = 1.0 / count
        for k in range(count):
            t = k / SAMPLE_RATE
            f = freq * (ratio ** (k * inv_count)) if freq_end else freq
            if vibrato:
                f *= 1.0 + vibrato * sin(two_pi * 5.5 * t)
            phase += f / SAMPLE_RATE
            p = phase - int(phase)
            if wave_shape == "sine":
                s = sin(two_pi * p)
                if harmonics:
                    for order, weight in harmonics:
                        s += weight * sin(two_pi * p * order)
            elif wave_shape == "saw":
                s = 2.0 * p - 1.0
            elif wave_shape == "square":
                s = 1.0 if p < 0.5 else -1.0
            elif wave_shape == "triangle":
                s = 1.0 - 4.0 * abs(p - 0.5)
            else:  # noise
                s = self.rng.uniform(-1.0, 1.0)
            if lowpass is not None:
                cutoff = lowpass(t) if callable(lowpass) else lowpass
                filtered += cutoff * (s - filtered)
                s = filtered
            env = min(1.0, k / attack_n)
            if decay:
                env *= exp(-t / decay)
            remaining = count - k
            if remaining < release_n:
                env *= remaining / release_n
            buf[first + k] += amp * env * s

    def kick(self, start):
        self.tone(start, 0.32, 150.0, 0.45, freq_end=42.0, decay=0.11, release=0.02)
        self.tone(start, 0.004, 1.0, 0.25, wave_shape="noise", release=0.002)

    def snare(self, start):
        self.tone(start, 0.2, 1.0, 0.32, wave_shape="noise", decay=0.055, lowpass=0.55)
        self.tone(start, 0.12, 190.0, 0.22, decay=0.045)

    def hat(self, start, amp=0.12):
        # Noise minus its low-passed self = crude high-pass.
        first = int(start * SAMPLE_RATE)
        count = min(int(0.06 * SAMPLE_RATE), len(self.samples) - first)
        low = 0.0
        for k in range(max(0, count)):
            x = self.rng.uniform(-1.0, 1.0)
            low += 0.25 * (x - low)
            self.samples[first + k] += amp * (x - low) * math.exp(-k / (0.014 * SAMPLE_RATE))


def render_song(length_s):
    """Returns (tracks, chart_events). chart_events[lane] = {time_ms: column bits}."""
    tracks = {name: Track(length_s, seed) for seed, name in enumerate(LANES)}
    events = {name: {} for name in LANES}

    def note(lane, time_s, columns):
        time_ms = round(time_s * 1000.0, 1)
        events[lane][time_ms] = events[lane].get(time_ms, 0) | columns

    drums, synth, bass, vocals, pad, fx = (tracks[name] for name in LANES)

    for bar in range(BARS):
        chord = PROGRESSION[bar % 4]
        root = BASS_ROOTS[bar % 4]
        in_a, in_b, in_c = bar in SECTION_A, bar in SECTION_B, bar in SECTION_C

        # ---- Drums
        if bar in INTRO or bar in OUTRO:
            for eighth in range(8):
                drums.hat(beat_time(bar, eighth * 0.5), 0.08)
        else:
            for eighth in range(8):
                drums.hat(beat_time(bar, eighth * 0.5), 0.12 if eighth % 2 else 0.07)
            kicks = [0, 1, 2, 3] if in_c else [0, 2]
            for beat in kicks:
                drums.kick(beat_time(bar, beat))
            for beat in (1, 3):
                drums.snare(beat_time(bar, beat))
            # Kick on the left button, snare in the middle, the closing open hat on the right.
            for beat in kicks:
                note("drums", beat_time(bar, beat), LEFT)
            for beat in (1, 3):
                note("drums", beat_time(bar, beat), MIDDLE)
            note("drums", beat_time(bar, 3.5), RIGHT)

        # ---- Bass: driving eighths with an octave on the off-beats
        if in_a or in_b or in_c:
            for eighth in range(8):
                midi = root + (12 if eighth % 2 else 0)
                bass.tone(beat_time(bar, eighth * 0.5), BEAT * 0.45, midi_to_hz(midi), 0.26, wave_shape="saw",
                          attack=0.003, decay=0.25, lowpass=0.12)
            # Root on the left, the off-beat octave jump on the right.
            for beat, column in ((0, LEFT), (1, MIDDLE), (1.5, RIGHT), (2, MIDDLE), (3, LEFT)):
                note("bass", beat_time(bar, beat), column)

        # ---- Synth: sixteenth-note arpeggio through the chord
        if in_b or in_c:
            arp = [chord[0] + 12, chord[1] + 12, chord[2] + 12, chord[0] + 24]
            for sixteenth in range(16):
                midi = arp[sixteenth % 4]
                synth.tone(beat_time(bar, sixteenth * 0.25), BEAT * 0.22, midi_to_hz(midi), 0.13, wave_shape="square",
                           decay=0.07, lowpass=0.28)
            # Arpeggio steps: chord root left, third/fifth middle, octave right.
            for sixteenth in (0, 3, 6, 8, 11, 14):
                column = (LEFT, MIDDLE, MIDDLE, RIGHT)[sixteenth % 4]
                note("synth", beat_time(bar, sixteenth * 0.25), column)

        # ---- Pad: sustained chord, fading in during the intro
        pad_amp = 0.035 if bar not in INTRO else 0.02 + 0.01 * bar
        for midi in chord:
            for detune in (0.996, 1.004):
                pad.tone(beat_time(bar), BAR + 0.25, midi_to_hz(midi) * detune, pad_amp, wave_shape="saw",
                         attack=0.25, release=0.3, lowpass=0.05)
        pad_column = (MIDDLE, LEFT, RIGHT, MIDDLE)[bar % 4]
        note("pad", beat_time(bar), pad_column)
        if in_c:
            note("pad", beat_time(bar, 2), RIGHT if pad_column == LEFT else LEFT)

        # ---- FX: impacts, zaps and risers
        if bar in (SECTION_A.start, SECTION_B.start, SECTION_C.start):
            fx.tone(beat_time(bar), 0.6, 60.0, 0.4, decay=0.25)
            fx.tone(beat_time(bar), 0.3, 1.0, 0.18, wave_shape="noise", decay=0.1, lowpass=0.3)
            note("fx", beat_time(bar), LEFT | MIDDLE | RIGHT)
        if in_c and bar % 2 == 1:
            for beat, column in ((1.5, LEFT), (3.5, RIGHT)):
                fx.tone(beat_time(bar, beat), 0.15, 2200.0, 0.12, freq_end=220.0, decay=0.08)
                note("fx", beat_time(bar, beat), column)
        if bar + 1 in (SECTION_B.start, SECTION_C.start, OUTRO.start):
            # One-bar noise riser into the next section.
            fx.tone(beat_time(bar), BAR, 1.0, 0.16, wave_shape="noise", attack=BAR * 0.9, release=0.02,
                    lowpass=lambda t: 0.02 + 0.5 * min(1.0, t / BAR))
            note("fx", beat_time(bar, 3), MIDDLE)

    # ---- Vocals: a sung-sounding lead (sine + harmonics + vibrato) over sections B and C
    melody = [  # (bar offset, beat, length in beats, midi)
        (0, 0, 1.5, 69), (0, 1.5, 0.5, 72), (0, 2, 2, 76),
        (1, 0, 1, 74), (1, 1, 1, 72), (1, 2, 2, 69),
        (2, 0, 1.5, 67), (2, 1.5, 0.5, 69), (2, 2, 2, 72),
        (3, 0, 1, 74), (3, 1, 1, 71), (3, 2, 2, 67),
    ]
    for phrase_start in (SECTION_B.start, SECTION_B.start + 4, SECTION_C.start, SECTION_C.start + 4):
        for bar_offset, beat, length, midi in melody:
            start = beat_time(phrase_start + bar_offset, beat)
            vocals.tone(start, length * BEAT * 0.95, midi_to_hz(midi), 0.14, attack=0.04, release=0.08,
                        vibrato=0.012, harmonics=[(2, 0.35), (3, 0.15), (4, 0.05)])
            note("vocals", start, LEFT if midi <= 69 else (MIDDLE if midi <= 72 else RIGHT))

    # Final hit on every instrument.
    final = beat_time(OUTRO.stop - 1, 0)
    fx.tone(final, 1.8, 55.0, 0.45, decay=0.6)
    drums.kick(final)
    note("drums", final, LEFT)
    note("fx", final, LEFT | MIDDLE | RIGHT)

    return tracks, events


def build_chart(events):
    """One chart entry per instrument hit: {"lane": 1-6, "column": 1-3 or [..] for chords}."""
    entries = []
    for lane_index, lane in enumerate(LANES):
        for time_ms, bits in events[lane].items():
            entries.append((time_ms, lane_index, bits))
    entries.sort()

    notes = []
    for time_ms, lane_index, bits in entries:
        columns = [column + 1 for column in range(3) if bits & (1 << column)]
        notes.append({
            "id": len(notes) + 1,
            "time_ms": time_ms,
            "lane": lane_index + 1,
            "column": columns[0] if len(columns) == 1 else columns,
            "type": {1: "single", 2: "double"}.get(len(columns), "triple"),
        })
    return notes


def write_json(path, notes, length_s):
    duration_ms = int(round(length_s * 1000))
    song = {
        "metadata": {
            "title": "Neon Drive",
            "artist": "Amplitude Demo",
            "album": "Procedural Sessions",
            "year": 2024,
            "duration_ms": duration_ms,
            "bpm": BPM,
            "version": "1.0",
            "rating": 2,
        },
        "audio": {
            "file_path": "audio.wav",
            "channels": {"drums": 0, "synth": 1, "bass": 2, "vocals": 3, "pad": 4, "fx": 5, "master": 6},
            "sample_rate": SAMPLE_RATE,
            "duration_samples": int(length_s * SAMPLE_RATE),
        },
        "difficulties": {
            "mellow": {"note_density_multiplier": 0.4, "note_speed": 300, "perfect_window_ms": 150, "good_window_ms": 400,
                       "energy_on_good": 1, "energy_on_miss": -1},
            "normal": {"note_density_multiplier": 1.0, "note_speed": 500, "perfect_window_ms": 100, "good_window_ms": 300,
                       "energy_on_good": 1, "energy_on_miss": -3},
            "brutal": {"note_density_multiplier": 1.5, "note_speed": 700, "perfect_window_ms": 75, "good_window_ms": 250,
                       "energy_on_good": 1, "energy_on_miss": -4},
            "insane": {"note_density_multiplier": 2.0, "note_speed": 900, "perfect_window_ms": 50, "good_window_ms": 200,
                       "energy_on_good": 1, "energy_on_miss": -4},
        },
        "rules": RULES,
        "notes": notes,
        "events": {
            "beat_markers": [{"time_ms": round(beat * BEAT * 1000.0, 1), "beat": beat} for beat in range(BARS * 4)],
        },
    }
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(song, handle, indent=1)
        handle.write("\n")


def write_wav(path, tracks, length_s):
    frames = int(length_s * SAMPLE_RATE)
    channels = [tracks[name].samples for name in LANES]
    master = [0.0] * frames
    for samples in channels:
        for index in range(frames):
            master[index] += samples[index]
    channels.append([0.8 * value for value in master])

    count = len(channels)
    pcm = array.array("h", bytes(2 * frames * count))
    for channel, samples in enumerate(channels):
        peak = max(1e-9, max(abs(value) for value in samples))
        if peak > 0.99:
            print(f"  note: channel {channel} peaks at {peak:.2f}, soft-limiting", file=sys.stderr)
        for index in range(frames):
            value = samples[index]
            if value > 0.99 or value < -0.99:
                value = math.tanh(value)
            pcm[index * count + channel] = int(value * 32767.0)

    if sys.byteorder != "little":
        pcm.byteswap()
    with wave.open(path, "wb") as handle:
        handle.setnchannels(count)
        handle.setsampwidth(2)
        handle.setframerate(SAMPLE_RATE)
        handle.writeframes(pcm.tobytes())


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output-dir", default=os.path.join(root, "Songs", "neon_drive"))
    parser.add_argument("--chart-only", action="store_true", help="only write song.json")
    args = parser.parse_args()

    os.makedirs(args.output_dir, exist_ok=True)
    length_s = BARS * BAR + TAIL

    print("Synthesising Neon Drive..." if not args.chart_only else "Building chart...")
    tracks, events = render_song(length_s if not args.chart_only else 0.0)
    notes = build_chart(events)
    json_path = os.path.join(args.output_dir, "song.json")
    write_json(json_path, notes, length_s)
    print(f"  wrote {json_path} ({len(notes)} chart entries)")

    if not args.chart_only:
        wav_path = os.path.join(args.output_dir, "audio.wav")
        write_wav(wav_path, tracks, length_s)
        print(f"  wrote {wav_path} ({length_s:.1f} s, 7 channels)")


if __name__ == "__main__":
    main()
