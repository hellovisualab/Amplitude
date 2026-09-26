#!/usr/bin/env python3
"""Generates "Neon Drive", the demo song bundled with Amplitude.

The song is synthesised from scratch (no samples), so it can be regenerated anywhere:

  * audio.wav  - 7-channel 16-bit 44.1 kHz multitrack WAV in the layout of spec 10.1.1
                 (0 drums, 1 synth, 2 bass, 3 vocals, 4 pad, 5 fx, 6 master/click reference)
  * song.json  - metadata + chart (spec 14.1). The chart is derived from the same musical events
                 that are rendered into the audio, so every note lines up with an instrument hit.

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

# Lanes featured in the chart for each 2-bar phrase (Amplitude-style: the player hops between phrases).
PHRASES = {
    2: ["drums"], 4: ["bass"], 6: ["drums", "bass"], 8: ["bass", "pad"],
    10: ["synth"], 12: ["vocals"], 14: ["synth", "drums"], 16: ["vocals", "fx"],
    18: ["drums", "fx"], 20: ["synth", "pad"], 22: ["bass", "vocals"], 24: ["drums", "synth", "fx"],
}


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
    """Returns (tracks, chart_events). chart_events[lane] = list of (time_s, strength)."""
    tracks = {name: Track(length_s, seed) for seed, name in enumerate(LANES)}
    events = {name: [] for name in LANES}

    def note(lane, time_s):
        events[lane].append(round(time_s * 1000.0, 1))

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
            for beat in sorted(set(kicks + [1, 3])):
                note("drums", beat_time(bar, beat))

        # ---- Bass: driving eighths with an octave on the off-beats
        if in_a or in_b or in_c:
            for eighth in range(8):
                midi = root + (12 if eighth % 2 else 0)
                bass.tone(beat_time(bar, eighth * 0.5), BEAT * 0.45, midi_to_hz(midi), 0.26, wave_shape="saw",
                          attack=0.003, decay=0.25, lowpass=0.12)
            for beat in (0, 1, 1.5, 2, 3):
                note("bass", beat_time(bar, beat))

        # ---- Synth: sixteenth-note arpeggio through the chord
        if in_b or in_c:
            arp = [chord[0] + 12, chord[1] + 12, chord[2] + 12, chord[0] + 24]
            for sixteenth in range(16):
                midi = arp[sixteenth % 4]
                synth.tone(beat_time(bar, sixteenth * 0.25), BEAT * 0.22, midi_to_hz(midi), 0.13, wave_shape="square",
                           decay=0.07, lowpass=0.28)
            for eighth in (0, 2, 3, 4, 6):
                note("synth", beat_time(bar, eighth * 0.5))

        # ---- Pad: sustained chord, fading in during the intro
        pad_amp = 0.035 if bar not in INTRO else 0.02 + 0.01 * bar
        for midi in chord:
            for detune in (0.996, 1.004):
                pad.tone(beat_time(bar), BAR + 0.25, midi_to_hz(midi) * detune, pad_amp, wave_shape="saw",
                         attack=0.25, release=0.3, lowpass=0.05)
        note("pad", beat_time(bar))
        if in_c:
            note("pad", beat_time(bar, 2))

        # ---- FX: impacts, zaps and risers
        if bar in (SECTION_A.start, SECTION_B.start, SECTION_C.start):
            fx.tone(beat_time(bar), 0.6, 60.0, 0.4, decay=0.25)
            fx.tone(beat_time(bar), 0.3, 1.0, 0.18, wave_shape="noise", decay=0.1, lowpass=0.3)
            note("fx", beat_time(bar))
        if in_c and bar % 2 == 1:
            for beat in (1.5, 3.5):
                fx.tone(beat_time(bar, beat), 0.15, 2200.0, 0.12, freq_end=220.0, decay=0.08)
                note("fx", beat_time(bar, beat))
        if bar + 1 in (SECTION_B.start, SECTION_C.start, OUTRO.start):
            # One-bar noise riser into the next section.
            fx.tone(beat_time(bar), BAR, 1.0, 0.16, wave_shape="noise", attack=BAR * 0.9, release=0.02,
                    lowpass=lambda t: 0.02 + 0.5 * min(1.0, t / BAR))
            note("fx", beat_time(bar, 3))

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
            note("vocals", start)

    # Final hit on every instrument.
    final = beat_time(OUTRO.stop - 1, 0)
    fx.tone(final, 1.8, 55.0, 0.45, decay=0.6)
    drums.kick(final)
    note("drums", final)
    note("fx", final)

    return tracks, events


def build_chart(events):
    """Keeps only the featured lanes of each phrase and merges simultaneous notes into chords.

    Chords are kept on bar downbeats only; elsewhere the phrase's first featured lane wins, which
    keeps Normal readable. Harder difficulties add chords procedurally (extra_chord_ratio).
    """
    featured = {}
    for start_bar, lanes in PHRASES.items():
        for bar in (start_bar, start_bar + 1):
            featured[bar] = lanes
    # Intro/outro: pad and fx carry the melody-less bars.
    for bar in list(INTRO) + list(OUTRO):
        featured[bar] = ["pad", "fx"]

    by_time = {}
    for lane in LANES:
        for time_ms in events[lane]:
            bar = min(BARS - 1, int(time_ms / 1000.0 / BAR + 1e-6))
            is_final = bar == OUTRO.stop - 1 and lane in ("drums", "fx")
            if lane not in featured.get(bar, []) and not is_final:
                continue
            by_time.setdefault(time_ms, set()).add(lane)

    notes = []
    for time_ms in sorted(by_time):
        bar = min(BARS - 1, int(time_ms / 1000.0 / BAR + 1e-6))
        downbeat = abs(time_ms - beat_time(bar) * 1000.0) < 1.0
        order = featured.get(bar, LANES)
        lanes = sorted(by_time[time_ms], key=lambda name: order.index(name) if name in order else len(order))
        if not downbeat:
            lanes = lanes[:1]
        lane_numbers = sorted(LANES.index(name) + 1 for name in lanes[:3])
        notes.append({
            "id": len(notes) + 1,
            "time_ms": time_ms,
            "lane": lane_numbers[0] if len(lane_numbers) == 1 else lane_numbers,
            "type": {1: "single", 2: "double"}.get(len(lane_numbers), "triple"),
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
