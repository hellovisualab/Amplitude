#!/usr/bin/env python3
"""Imports one of your own songs into Amplitude.

The game plays every instrument from its own track (that is how a song builds up as you play it), so
a finished stereo mix has to be split into instrument stems first. This script does the whole job:

  1. Separation. Either an AI model (Demucs, by Meta) splits the song into drums, bass, vocals,
     guitar, piano and "other", or you give it the stems you already have (your own multitracks, or
     the output of a separation app such as Ultimate Vocal Remover).
  2. Chart. It finds the tempo and the notes of every instrument (onset detection), snaps them to
     the beat, spreads them over the three gem columns from low to high pitch, and thins them to a
     playable density.
  3. Install. It writes Songs/<id>/ with one WAV per instrument and song.json, ready to play.

Usage (on Windows you can also drag a song, or a folder of stems, onto Tools/ImportSong.bat):
    python Tools/import_song.py "C:/Music/My Song.mp3"
    python Tools/import_song.py "My Song.mp3" --title "My Song" --artist "Me"
    python Tools/import_song.py --stems "C:/Music/My Song (stems)" --title "My Song"

Stem files are matched to lanes by name (drums, kick, snare, bass, vocal, vox, guitar, synth, lead,
piano, keys, pad, strings, fx, other...). Several files for one lane (kick.wav + snare.wav) are mixed.

Requirements (Python 3.9+):
    pip install numpy imageio-ffmpeg          # always (imageio-ffmpeg brings its own ffmpeg)
    pip install demucs                        # only to separate a finished mix (installs PyTorch)
With an NVIDIA card, installing the CUDA build of PyTorch first (see pytorch.org) makes separation
several times faster; on a CPU expect a few minutes per song.
"""

import argparse
import bisect
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import wave

try:
    import numpy as np
except ImportError:
    sys.exit("This tool needs numpy:  pip install numpy imageio-ffmpeg")

SAMPLE_RATE = 44100
LANES = ["drums", "synth", "bass", "vocals", "pad", "fx"]
AUDIO_EXTENSIONS = (".wav", ".mp3", ".flac", ".ogg", ".m4a", ".aac", ".aif", ".aiff", ".opus", ".wma")

# Demucs source name -> game lane. The 4-source models have no guitar/piano: "other" becomes the synth lane.
DEMUCS_LANES_6 = {"drums": "drums", "bass": "bass", "vocals": "vocals", "guitar": "synth", "piano": "pad", "other": "fx"}
DEMUCS_LANES_4 = {"drums": "drums", "bass": "bass", "vocals": "vocals", "other": "synth"}

# Stem file name keywords -> lane, checked in this order.
STEM_KEYWORDS = [
    ("drums", ["drum", "kick", "snare", "hat", "perc", "beat", "cymbal", "tom", "clap"]),
    ("bass", ["bass", "808", "sub"]),
    ("vocals", ["vocal", "vox", "voice", "choir", "sing", "acapella", "adlib"]),
    ("pad", ["piano", "keys", "key", "pad", "string", "organ", "rhodes", "chord", "epiano"]),
    ("synth", ["synth", "lead", "guitar", "gtr", "arp", "pluck", "melody", "brass", "horn"]),
    ("fx", ["fx", "sfx", "other", "riser", "effect", "noise", "ambience", "ambient", "atmo", "impact"]),
]

# Chart tuning. The base chart is the Normal difficulty; the game derives the others from it.
ANALYSIS_RATE = SAMPLE_RATE // 2
FFT_SIZE = 1024
HOP = 256
FPS = ANALYSIS_RATE / HOP
MIN_GAP_S = 0.14            # never two gems of one lane closer than this
MAX_NOTES_PER_SECOND = 3.0  # per lane, averaged over a 2 s window
SNAP_TOLERANCE_S = 0.045    # onsets this close to the 16th-note grid are snapped onto it
SILENCE_DB = -30.0          # below this (relative to the stem's loud parts) a stem counts as silent
MIN_NOTES_PER_SECOND = 0.6  # while a stem plays; quieter or slower instruments get a lower detection threshold
# Frame times refer to the start of the analysis window; an attack is centred in it.
FRAME_OFFSET_S = FFT_SIZE / 2.0 / ANALYSIS_RATE
# Attack detection: (threshold over the local average, absolute floor).
STRICT = (1.35, 0.05)
CHORD_CHANGE_WINDOW_S = 0.25
CHROMA_FFT_SIZE = 4096
CHORD_CHANGE_THRESHOLD = 0.05


def log(message):
    print(message, flush=True)


# ---------------------------------------------------------------------------------------------
# Audio I/O

def find_ffmpeg():
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except Exception:
        pass
    path = shutil.which("ffmpeg")
    if path:
        return path
    sys.exit("ffmpeg was not found. Install it with:  pip install imageio-ffmpeg")


def load_audio(path, ffmpeg):
    """Any audio file -> float32 array (2, frames) at 44.1 kHz."""
    command = [ffmpeg, "-v", "error", "-i", path, "-f", "f32le", "-ac", "2", "-ar", str(SAMPLE_RATE), "-"]
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        sys.exit(f"Could not read {path}:\n{result.stderr.decode(errors='replace')}")
    samples = np.frombuffer(result.stdout, dtype=np.float32)
    return samples.reshape(-1, 2).T.copy()


def write_wav(path, audio):
    """(channels, frames) float -> 16-bit PCM WAV."""
    clipped = np.clip(audio, -1.0, 1.0)
    interleaved = (clipped.T * 32767.0).astype("<i2")
    with wave.open(path, "wb") as handle:
        handle.setnchannels(audio.shape[0])
        handle.setsampwidth(2)
        handle.setframerate(SAMPLE_RATE)
        handle.writeframes(interleaved.tobytes())


# ---------------------------------------------------------------------------------------------
# Separation

def separate_with_demucs(mix, model_name, device):
    try:
        import torch
        from demucs.apply import apply_model
        from demucs.pretrained import get_model
    except ImportError:
        sys.exit("Separating a finished mix needs Demucs:  pip install demucs\n"
                 "(or split the song with another app and use --stems FOLDER)")

    if device == "auto":
        device = "cuda" if torch.cuda.is_available() else "cpu"
    log(f"Separating instruments with Demucs ({model_name}, {device}). The first run downloads the model...")
    model = get_model(model_name)
    model.eval()
    if getattr(model, "samplerate", SAMPLE_RATE) != SAMPLE_RATE:
        sys.exit(f"Model {model_name} expects {model.samplerate} Hz; use htdemucs_6s or htdemucs.")

    wav = torch.from_numpy(mix)
    reference = wav.mean(0)
    mean, std = reference.mean(), reference.std() + 1e-8
    with torch.no_grad():
        sources = apply_model(model, ((wav - mean) / std)[None], device=device, shifts=1, split=True, overlap=0.25, progress=True)[0]
    sources = (sources * std + mean).cpu().numpy()

    mapping = DEMUCS_LANES_6 if "guitar" in model.sources else DEMUCS_LANES_4
    stems = {}
    for index, name in enumerate(model.sources):
        lane = mapping.get(name)
        if lane:
            stems[lane] = stems.get(lane, 0) + sources[index]
    return stems


def lane_for_file(name):
    lower = os.path.splitext(os.path.basename(name))[0].lower()
    for lane, keywords in STEM_KEYWORDS:
        if any(keyword in lower for keyword in keywords):
            return lane
    return None


def load_stem_folder(folder, ffmpeg):
    files = sorted(os.path.join(folder, name) for name in os.listdir(folder) if name.lower().endswith(AUDIO_EXTENSIONS))
    if not files:
        sys.exit(f"No audio files found in {folder}")
    stems = {}
    unmatched = []
    for path in files:
        lane = lane_for_file(path)
        if lane is None:
            unmatched.append(path)
            continue
        log(f"  {os.path.basename(path)} -> {lane}")
        audio = load_audio(path, ffmpeg)
        stems[lane] = add_audio(stems.get(lane), audio)
    for path in unmatched:
        # Unnamed stems fill the free lanes, extras go to FX.
        lane = next((name for name in ["synth", "pad", "fx"] if name not in stems), "fx")
        log(f"  {os.path.basename(path)} -> {lane} (name not recognised)")
        stems[lane] = add_audio(stems.get(lane), load_audio(path, ffmpeg))
    return stems


def add_audio(existing, audio):
    if existing is None:
        return audio
    length = max(existing.shape[1], audio.shape[1])
    result = np.zeros((2, length), dtype=np.float32)
    result[:, :existing.shape[1]] += existing
    result[:, :audio.shape[1]] += audio
    return result


# ---------------------------------------------------------------------------------------------
# Analysis

class Analysis:
    """Onset envelope, loudness and brightness of one track at FPS frames per second."""

    def __init__(self, audio):
        mono = audio.mean(axis=0)
        mono = mono[: len(mono) // 2 * 2]
        mono = 0.5 * (mono[0::2] + mono[1::2])  # 22.05 kHz is plenty for finding notes
        frames = max(1, (len(mono) - FFT_SIZE) // HOP + 1)
        window = np.hanning(FFT_SIZE).astype(np.float32)
        frequencies = np.fft.rfftfreq(FFT_SIZE, 1.0 / ANALYSIS_RATE)

        self.flux = np.zeros(frames, dtype=np.float32)
        self.centroid = np.zeros(frames, dtype=np.float32)
        self.rms_db = np.full(frames, -120.0, dtype=np.float32)
        previous = None
        chunk = 2048
        for start in range(0, frames, chunk):
            count = min(chunk, frames - start)
            index = (start + np.arange(count))[:, None] * HOP + np.arange(FFT_SIZE)[None, :]
            index = np.minimum(index, len(mono) - 1)
            block = mono[index] * window
            magnitude = np.abs(np.fft.rfft(block, axis=1)).astype(np.float32)
            compressed = np.log1p(20.0 * magnitude)
            if previous is None:
                previous = compressed[:1]
            difference = np.diff(np.vstack([previous, compressed]), axis=0)
            self.flux[start:start + count] = np.maximum(difference, 0.0).sum(axis=1)
            previous = compressed[-1:]
            energy = magnitude.sum(axis=1) + 1e-9
            self.centroid[start:start + count] = (magnitude * frequencies[None, :]).sum(axis=1) / energy
            rms = np.sqrt((block ** 2).mean(axis=1)) + 1e-9
            self.rms_db[start:start + count] = 20.0 * np.log10(rms)
        loud = np.percentile(self.rms_db, 95)
        self.relative_db = self.rms_db - loud
        self.loud_db = loud
        self.active_seconds = float((self.relative_db > SILENCE_DB).sum()) / FPS
        self.chroma = compute_chroma(mono, frames)


def compute_chroma(mono, frames):
    """12 pitch-class energies per analysis frame. Uses a long window: low notes need the resolution."""
    size = CHROMA_FFT_SIZE
    frequencies = np.fft.rfftfreq(size, 1.0 / ANALYSIS_RATE)
    tonal = (frequencies >= 70.0) & (frequencies <= 2500.0)
    pitch = 12.0 * np.log2(np.maximum(frequencies[tonal], 1.0) / 440.0)
    classes = np.round(pitch).astype(int) % 12
    chroma_map = np.zeros((int(tonal.sum()), 12), dtype=np.float32)
    chroma_map[np.arange(len(classes)), classes] = 1.0
    window = np.hanning(size).astype(np.float32)
    padded = np.pad(mono, (size // 2, size))
    chroma = np.zeros((frames, 12), dtype=np.float32)
    step = 2  # every other frame is plenty for harmony; filled in below
    for start in range(0, frames, 1024):
        rows = np.arange(start, min(frames, start + 1024), step)
        index = rows[:, None] * HOP + np.arange(size)[None, :]
        block = padded[np.minimum(index, len(padded) - 1)] * window
        magnitude = np.abs(np.fft.rfft(block, axis=1))[:, tonal].astype(np.float32)
        values = np.log1p(10.0 * magnitude) @ chroma_map
        values /= np.linalg.norm(values, axis=1, keepdims=True) + 1e-9
        chroma[rows] = values
        chroma[np.minimum(rows + 1, frames - 1)] = values
    return chroma


def normalise(envelope):
    top = np.percentile(envelope, 99.5)
    return envelope / top if top > 0 else envelope


def estimate_tempo(envelope, bpm_hint=None):
    """Returns (bpm, first beat time in seconds, index of the first downbeat among beats 0-3)."""
    env = normalise(envelope) - normalise(envelope).mean()
    if bpm_hint:
        bpm = bpm_hint
    else:
        # Coarse: autocorrelation over 60-200 BPM, favouring tempos around 120.
        lags = np.arange(int(FPS * 60 / 200), int(FPS * 60 / 60) + 1)
        values = np.array([np.dot(env[:-lag], env[lag:]) for lag in lags])
        bpms = 60.0 * FPS / lags
        weights = np.exp(-0.5 * (np.log2(bpms / 120.0) / 0.9) ** 2)
        bpm = float(bpms[np.argmax(values * weights)])
        while bpm < 80:
            bpm *= 2
        while bpm > 170:
            bpm /= 2

    # Fine: comb search for the tempo and phase that best line up with the onsets over the whole song.
    positive = np.maximum(env, 0)
    duration = len(env) / FPS
    candidates = [bpm] if bpm_hint else np.arange(bpm * 0.985, bpm * 1.015, 0.01)
    best = (-1.0, bpm, 0.0)
    for candidate in candidates:
        period = FPS * 60.0 / candidate
        beats = np.arange(int(duration * candidate / 60.0))
        phases = np.arange(0.0, period, 0.5)
        positions = phases[:, None] + beats[None, :] * period
        scores = np.interp(positions, np.arange(len(positive)), positive, right=0.0).mean(axis=1)
        index = int(np.argmax(scores))
        if scores[index] > best[0]:
            best = (float(scores[index]), float(candidate), float(phases[index]))
    _, bpm, phase = best
    if not bpm_hint:
        bpm = round(bpm, 2)
    return bpm, phase / FPS + FRAME_OFFSET_S


def find_first_bar(analysis, bpm, first_beat_s):
    """Songs almost always start on a bar: the beat nearest the first real sound is taken as bar 1.
    (Only the visuals use bars; notes keep their own times.)"""
    beat = 60.0 / bpm
    sounding = np.where(analysis.relative_db > -35.0)[0]
    start_s = sounding[0] / FPS + FRAME_OFFSET_S if len(sounding) else first_beat_s
    return first_beat_s + round((start_s - first_beat_s) / beat) * beat


def pick_onsets(analysis, ratio, floor):
    """(time_s, strength, centroid) for every note-like onset where the stem is actually playing."""
    env = normalise(analysis.flux)
    width = int(0.6 * FPS)
    local_mean = np.convolve(env, np.ones(2 * width + 1) / (2 * width + 1), mode="same")
    padded = np.pad(env, 3, mode="edge")
    local_max = np.lib.stride_tricks.sliding_window_view(padded, 7).max(axis=1)
    candidates = np.where((env >= local_max) & (env > local_mean * ratio + floor) & (analysis.relative_db > SILENCE_DB))[0]
    onsets = []
    for frame in candidates:
        # The note's brightness a few frames in, once the attack has settled.
        look = min(len(analysis.centroid) - 1, frame + 3)
        onsets.append((frame / FPS + FRAME_OFFSET_S, float(env[frame]), float(analysis.centroid[look])))
    return onsets


def pick_chord_changes(analysis):
    """Onsets of sustained instruments: frames where the notes being played change (chroma novelty)."""
    width = max(2, int(CHORD_CHANGE_WINDOW_S * FPS))
    frames = len(analysis.chroma)
    if frames <= 2 * width:
        return []
    cumulative = np.vstack([np.zeros((1, 12), dtype=np.float64), np.cumsum(analysis.chroma, axis=0, dtype=np.float64)])
    index = np.arange(width, frames - width)
    before = cumulative[index] - cumulative[index - width]
    after = cumulative[index + width] - cumulative[index]
    similarity = (before * after).sum(axis=1) / (np.linalg.norm(before, axis=1) * np.linalg.norm(after, axis=1) + 1e-9)
    novelty = np.zeros(frames, dtype=np.float32)
    novelty[index] = 1.0 - similarity
    novelty[analysis.relative_db <= SILENCE_DB] = 0.0

    span = int(0.25 * FPS)
    padded = np.pad(novelty, span, mode="constant")
    local_max = np.lib.stride_tricks.sliding_window_view(padded, 2 * span + 1).max(axis=1)
    peaks = np.where((novelty >= local_max) & (novelty > CHORD_CHANGE_THRESHOLD))[0]
    onsets = []
    for frame in peaks:
        # A change is centred between the two windows; the new chord starts at the window boundary.
        look = min(frames - 1, frame + width // 2)
        onsets.append((frame / FPS + FRAME_OFFSET_S, float(novelty[frame]), float(analysis.centroid[look])))
    return onsets


def pick_sustained(analysis, bpm, first_bar_s):
    """Gems for a held instrument: one every half bar while it sounds, plus its chord changes, on the beat."""
    beat = 60.0 / bpm
    duration = len(analysis.relative_db) / FPS
    frames = np.arange(len(analysis.relative_db))

    def sounding(time_s):
        return np.interp((time_s - FRAME_OFFSET_S) * FPS, frames, analysis.relative_db) > SILENCE_DB + 6.0

    def centroid_at(time_s):
        return float(np.interp((time_s - FRAME_OFFSET_S) * FPS + 3, frames, analysis.centroid))

    start = first_bar_s - np.floor(first_bar_s / (2.0 * beat)) * 2.0 * beat
    onsets = [(float(time_s), 0.5, centroid_at(time_s)) for time_s in np.arange(start, duration, 2.0 * beat) if sounding(time_s)]
    for time_s, strength, _centroid in pick_chord_changes(analysis):
        # Slow attacks lag the beat they belong to; pull each change back onto the nearest beat.
        snapped = first_bar_s + round((time_s - first_bar_s - 0.1 * beat) / beat) * beat
        if 0.0 <= snapped < duration and sounding(snapped + 0.2 * beat):
            onsets.append((float(snapped), 1.0 + strength, centroid_at(snapped + 0.2 * beat)))
    return onsets


def thin_onsets(onsets):
    """Keeps the strongest onsets: a minimum gap and a cap on notes per second."""
    accepted = []
    for time_s, strength, centroid in sorted(onsets, key=lambda onset: -onset[1]):
        times = [note[0] for note in accepted]
        position = bisect.bisect_left(times, time_s)
        if position > 0 and time_s - times[position - 1] < MIN_GAP_S:
            continue
        if position < len(times) and times[position] - time_s < MIN_GAP_S:
            continue
        crowd = bisect.bisect_right(times, time_s + 1.0) - bisect.bisect_left(times, time_s - 1.0)
        if crowd >= MAX_NOTES_PER_SECOND * 2:
            continue
        accepted.insert(position, (time_s, strength, centroid))
    return accepted


def snap(time_s, bpm, first_beat_s):
    step = 60.0 / bpm / 4.0
    grid = first_beat_s + round((time_s - first_beat_s) / step) * step
    return grid if abs(grid - time_s) <= SNAP_TOLERANCE_S else time_s


def build_lane_notes(analysis, bpm, first_beat_s, snap_to_grid):
    # Instruments with clear attacks (drums, bass, plucks, vocals) are charted from their attacks.
    # Soft, sustained ones (pads, strings, organs) hardly have attacks: they get gems on the beat while
    # they sound and on their chord changes, plus the few clear attacks they have.
    attacks = pick_onsets(analysis, *STRICT)
    onsets = thin_onsets(attacks)
    if len(onsets) < MIN_NOTES_PER_SECOND * analysis.active_seconds:
        onsets = thin_onsets(attacks + pick_sustained(analysis, bpm, first_beat_s))
    if not onsets:
        return []
    # Columns from pitch: the lower third of this instrument's notes go left, the top third right.
    centroids = np.array([onset[2] for onset in onsets])
    low, high = np.percentile(centroids, [33.3, 66.7])
    notes = []
    last = -1.0
    for time_s, _strength, centroid in onsets:
        if snap_to_grid:
            time_s = snap(time_s, bpm, first_beat_s)
        if time_s - last < MIN_GAP_S * 0.9:
            continue
        column = 0 if centroid < low else (1 if centroid < high else 2)
        notes.append((time_s, column))
        last = time_s
    return notes


# ---------------------------------------------------------------------------------------------
# Output

def slugify(text):
    slug = re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")
    return slug or "my_song"


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("song", nargs="?", help="the finished song (mp3, wav, flac, ogg, m4a...)")
    parser.add_argument("--stems", help="folder with stems you already have (skips the AI separation)")
    parser.add_argument("--title", help="song title (default: file name)")
    parser.add_argument("--artist", default="", help="artist name")
    parser.add_argument("--id", help="folder name under Songs/ (default: from the title)")
    parser.add_argument("--bpm", type=float, help="tempo, if you know it (detected otherwise)")
    parser.add_argument("--offset-ms", type=float, default=0.0, help="per-song audio offset (+ if the audio is heard late)")
    parser.add_argument("--model", default="htdemucs_6s", help="Demucs model: htdemucs_6s (6 instruments) or htdemucs (4)")
    parser.add_argument("--device", default="auto", help="auto, cpu or cuda")
    parser.add_argument("--no-snap", action="store_true", help="keep note times exactly as detected")
    parser.add_argument("--out", default=os.path.join(root, "Songs"), help="songs folder")
    parser.add_argument("--save-mix", action="store_true", help="also write the separated stems' sum as mix.wav (for checking)")
    args = parser.parse_args()

    if not args.song and not args.stems:
        parser.error("give a song file, or --stems FOLDER")
    ffmpeg = find_ffmpeg()

    title = args.title or os.path.splitext(os.path.basename(args.song or os.path.normpath(args.stems)))[0]
    song_id = args.id or slugify(title)
    folder = os.path.join(args.out, song_id)
    os.makedirs(folder, exist_ok=True)

    # 1. Stems
    if args.stems:
        log(f"Reading stems from {args.stems}")
        stems = load_stem_folder(args.stems, ffmpeg)
    else:
        log(f"Reading {args.song}")
        stems = separate_with_demucs(load_audio(args.song, ffmpeg), args.model, args.device)

    length = max(audio.shape[1] for audio in stems.values())
    mix = np.zeros((2, length), dtype=np.float32)
    for audio in stems.values():
        mix[:, :audio.shape[1]] += audio
    peak = float(np.abs(mix).max()) or 1.0
    # All stems share one gain so they still add up to the original balance, just without clipping.
    gain = min(1.0, 0.98 / peak)

    # 2. Analysis and tempo
    log("Analysing...")
    analyses = {lane: Analysis(audio) for lane, audio in stems.items()}
    mix_analysis = Analysis(mix)
    beat_source = analyses["drums"].flux if "drums" in analyses and analyses["drums"].loud_db > -60 else mix_analysis.flux
    bpm, first_beat_s = estimate_tempo(beat_source + 0.5 * mix_analysis.flux[: len(beat_source)], args.bpm)
    first_bar_s = find_first_bar(mix_analysis, bpm, first_beat_s)
    log(f"  tempo {bpm:.2f} BPM, first bar at {first_bar_s:.2f} s")

    # 3. Stems and chart
    written = {}
    notes = []
    for lane in LANES:
        if lane not in stems:
            continue
        analysis = analyses[lane]
        if analysis.loud_db < -55.0:
            log(f"  {lane}: silent, skipped")
            continue
        file_name = f"{lane}.wav"
        write_wav(os.path.join(folder, file_name), stems[lane] * gain)
        written[lane] = file_name
        lane_notes = build_lane_notes(analysis, bpm, first_bar_s, not args.no_snap)
        log(f"  {lane}: {len(lane_notes)} notes")
        notes.extend((time_s, LANES.index(lane), column) for time_s, column in lane_notes)
    if args.save_mix:
        write_wav(os.path.join(folder, "mix.wav"), mix * gain)

    notes.sort()
    duration_ms = int(round(length * 1000.0 / SAMPLE_RATE))
    # The game counts bars from the first marker, so the markers start on the earliest downbeat.
    beat_ms = 60000.0 / bpm
    bar_ms = beat_ms * 4.0
    first_ms = max(0.0, first_bar_s * 1000.0) if first_bar_s > -0.06 else first_bar_s * 1000.0
    first_ms -= np.floor(first_ms / bar_ms) * bar_ms
    beat_markers = []
    while first_ms + len(beat_markers) * beat_ms < duration_ms:
        beat_markers.append({"time_ms": round(first_ms + len(beat_markers) * beat_ms, 1), "beat": len(beat_markers)})

    song = {
        "metadata": {
            "title": title,
            "artist": args.artist,
            "album": "",
            "year": datetime.date.today().year,
            "duration_ms": duration_ms,
            "bpm": bpm,
            "version": "1.0",
            "offset_ms": args.offset_ms,
        },
        "audio": {"stems": written},
        "rules": {"capture_streak": 8},
        "notes": [
            {"id": index + 1, "time_ms": round(time_s * 1000.0, 1), "lane": lane + 1, "column": column + 1}
            for index, (time_s, lane, column) in enumerate(notes)
        ],
        "events": {"beat_markers": beat_markers},
    }
    json_path = os.path.join(folder, "song.json")
    with open(json_path, "w", encoding="utf-8") as handle:
        json.dump(song, handle, indent=1, ensure_ascii=False)
        handle.write("\n")

    log(f"\nDone: {folder}")
    log(f"  {len(notes)} notes over {len(written)} instruments, {duration_ms / 1000.0:.0f} s, {bpm:.2f} BPM")
    log("  Start (or restart) the game and it appears in Select Song.")
    log("  Tip: in game, the console command amp.AutoPlay 1 plays every lane so you can check the chart.")


if __name__ == "__main__":
    main()
