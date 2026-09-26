# Amplitude

A lane-based rhythm-action game for PC (Windows/Linux) built with **Unreal Engine 5.4+** in C++,
implementing the *Amplitude: Rhythm-Action Game – Complete UE5 Implementation Specification v1.0*.

The player flies the **Beat Blaster** across six instrument lanes (Drums, Synth, Bass, Vocals, Pad, FX),
hitting notes in time with a multitrack song while managing energy, per-lane combos, lane capture,
muting and six powerups.

The whole game is code: rendering is Slate, sound effects are synthesised in real time, and songs are
loaded at runtime from JSON + WAV. The project has **no binary content assets** and runs on the engine's
built-in `Entry` map.

---

## Quick start

1. **Generate the demo song.** It is synthesised procedurally, so it is not checked in. This takes about 20 s and needs only Python 3:
   ```sh
   python3 Tools/generate_demo_song.py
   ```
   This writes `Songs/neon_drive/audio.wav`: 7 channels, 16-bit, 44.1 kHz, about 54 s.
   The matching `song.json` chart is already committed.
2. **Build.** Right-click `Amplitude.uproject` → *Generate Visual Studio project files*, then build
   `AmplitudeEditor` (Development Editor). You can also build from the command line:
   ```sh
   # Windows
   <UE>/Engine/Build/BatchFiles/Build.bat AmplitudeEditor Win64 Development -Project="%CD%/Amplitude.uproject"
   # Linux
   <UE>/Engine/Build/BatchFiles/Linux/Build.sh AmplitudeEditor Linux Development -Project="$PWD/Amplitude.uproject"
   ```
3. **Play.** Open the project and press *Play → Standalone Game*. Standalone is recommended because
   `Esc` ends a Play-In-Editor session. You can also launch it directly with `UnrealEditor Amplitude.uproject -game`.
4. **Package.** Use *Platforms → Windows/Linux → Package Project*. The `Songs/` folder is staged
   automatically as loose files (see `Amplitude.Build.cs`).

If the audio file is missing, the song select screen still lists the song. Starting it offers
*Play Without Audio*: the chart runs on the wall clock.

## Controls (spec §8, §18)

Pressing a lane key moves the Beat Blaster to that lane **and** fires at its hit zone. Press several
keys together for double and triple notes.

| Action | Primary | Alt 1 | Alt 2 | Gamepad |
|---|---|---|---|---|
| Lane 1 – Drums | A | 1 | ← | D-pad Left |
| Lane 2 – Synth | S | 2 | ↑ | D-pad Up |
| Lane 3 – Bass | D | 3 | → | D-pad Right |
| Lane 4 – Vocals | F | 4 | Space | X / Square |
| Lane 5 – Pad | G | 5 | R | Y / Triangle |
| Lane 6 – FX | H | 6 | T | B / Circle |
| Pause | P | Esc | Tab | Start |

In the menus, use the arrow keys, D-pad or mouse to navigate. Enter, Space or A/Cross confirms;
Esc, Backspace or B/Circle goes back. All gameplay bindings can be remapped in *Settings → Controls*.
Remapping supports several saved profiles and applies instantly.

## Project layout

```
Amplitude.uproject          UE 5.4 project (EnhancedInput plugin)
Config/                     Engine/game/input settings (Entry map, game mode, low-latency audio buffers)
Source/Amplitude/
  Core/                     Engine-agnostic C++20 gameplay core (namespace Amp), unit tested
    AmpTypes.h              Notes, chart entries, difficulty params, game rules
    AmpRules.*              Difficulty table, combo multipliers, judgement, scoring
    AmpChart.*              Chart normalisation, chord expansion, per-difficulty density scaling
    AmpSimulation.*         The game: hit detection, energy, combos, capture, mute, powerups, score
    AmpSongClock.*          Song clock slaved to the audio play head (drift slew / resync > 50 ms)
    AmpWav.*                RIFF/WAVE decoder (8/16/24/32-bit PCM, float, WAVE_FORMAT_EXTENSIBLE)
    AmpStemMixer.*          Real-time 6-track mixer: 200 ms mute crossfades, 0.5x slow motion, solo
    AmpSfxSynth.*           Procedural sound effects (ding, chime, buzz, sweep, sparkle, fanfare...)
  Audio/                    USynthComponent wrappers that run the mixer and SFX synth on the audio thread
  Data/                     Song library (JSON parsing, runtime WAV loading) and local leaderboards
  Game/                     Game mode, director (screen flow), play session, player controller, settings
  UI/                       Slate UI: playfield/HUD renderer, menus, settings, shared widgets and style
Songs/<song>/               Runtime song folders: song.json + audio
Tests/                      CMake unit tests for Source/Amplitude/Core
Tools/generate_demo_song.py Synthesises the "Neon Drive" demo song and its chart
```

### Runtime architecture

```
AAmplitudeGameMode ──spawns──▶ AAmplitudeDirector (actor)
                                 ├─ FAmplitudeSongLibrary   scans Songs/, parses song.json
                                 ├─ FAmplitudeLeaderboard   Saved/Amplitude/Leaderboards.json
                                 ├─ UAmplitudeStemPlayerComponent ─▶ Amp::FStemMixer (audio thread)
                                 ├─ UAmplitudeSfxComponent        ─▶ Amp::FSfxSynth  (audio thread)
                                 ├─ FAmplitudeSession (while playing)
                                 │    ├─ Amp::FSongClock   ◀── play-head snapshots from the stem player
                                 │    └─ Amp::FSimulation  ◀── timestamped lane presses
                                 └─ Slate root: SAmplitudeGameView (playfield + HUD) + current menu screen
AAmplitudePlayerController: Enhanced Input actions/mapping context built at runtime from the control profile
```

Each frame follows the order in spec §13.1. The session advances the clock and syncs it to the
audio play head. It then judges buffered presses at the song time they were made, and advances the
simulation (auto-play, auto-miss, captures, powerups, energy, completion). Finally it turns the
simulation's events into sound and visual effects.

**Timing.** The stem mixer publishes its sample-accurate play head after every audio buffer. The
song clock advances smoothly with wall time × playback rate and slews towards the audio position.
If the drift exceeds 50 ms, the clock snaps to the audio position (spec §10.2 and §13.3). Lane
presses are timestamped when Slate receives the key, not when Enhanced Input fires later in the
frame. This removes most of a frame of judging latency.

## Song format (spec §14.1, §20)

A song is a folder under `Songs/` containing `song.json` (or any `.json` file) and its audio. The
folder name is the song's ID for leaderboards. Everything in spec §14.1 and §20 is supported:

```jsonc
{
  "metadata": { "title": "...", "artist": "...", "album": "...", "year": 2024,
                "duration_ms": 240000, "bpm": 120, "version": "1.0",
                "rating": 4,            // optional 1-5 stars, estimated from note density otherwise
                "offset_ms": 0 },       // optional per-song audio offset (±500)
  "audio": {
    "file_path": "audio.wav",           // multichannel WAV; relative to the song folder, songs root or project
    "channels": { "drums": 0, "synth": 1, "bass": 2, "vocals": 3, "pad": 4, "fx": 5, "master": 6 },
    "stems": { "vocals": "vocals.wav" } // optional: per-instrument mono/stereo WAVs override channels
  },
  "difficulties": {                     // optional overrides of the built-in table (spec §15)
    "normal": { "note_density_multiplier": 1.0, "note_speed": 500, "perfect_window_ms": 100,
                "good_window_ms": 300, "energy_on_perfect": 2, "energy_on_good": 1, "energy_on_miss": -3,
                "combo_thresholds": [5, 10, 20, 50], "powerup_interval_ms": 10000,
                "powerup_weights": { "score_2x": 25, "fever": 15 }, "approach_time_ms": 2000,
                "extra_chord_ratio": 0.0 }
  },
  "rules": { "capture_streak": 4, "capture_duration_ms": 30000, "capture_energy_cost": 5,
             "mute_miss_streak": 4, "starting_energy": 50 },   // optional, defaults per spec
  "notes": [                            // base chart, authored at Normal density
    { "id": 1, "time_ms": 1000, "lane": 1, "type": "single" },
    { "id": 2, "time_ms": 2000, "lane": [1, 4], "type": "double" },
    { "id": 3, "time_ms": 3000, "lane": [1, 2, 3], "type": "triple" },
    { "id": 4, "time_ms": 4000, "lane": 2, "type": "stream", "count": 4, "interval_ms": 115 }
  ],
  "notes_brutal": [ ... ],              // optional hand-authored charts per difficulty (used as-is)
  "events": { "beat_markers": [ { "time_ms": 0, "beat": 0 } ] }
}
```

* **Lanes** are 1-6, or instrument names (`"drums"`, and so on). An array makes a chord.
* **Difficulty charts.** If `notes_<difficulty>` exists, it is used exactly as written. Otherwise the
  base chart (`notes`, or the nearest authored chart) is scaled by `note_density_multiplier`:
  * Mellow (40%) keeps an evenly spread subset.
  * Brutal (150%) and Insane (200%) subdivide gaps between notes, never closer than 180 ms.
  * Insane also turns 1 in 8 single notes into doubles.
* **Audio.** At runtime the game decodes **WAV only** (PCM 8/16/24/32-bit or float), which gives
  sample-accurate per-instrument mixing and muting. Convert OGG/MP3 multitracks to a multichannel WAV,
  for example `ffmpeg -i song.ogg -c:a pcm_s16le audio.wav`, or provide per-instrument stems. The
  master/click channel is loaded for reference but never played: the stems already make up the mix.
* Songs in extra folders are picked up when they are listed in `AdditionalSongDirectories`
  (GameUserSettings.ini).

## Testing

**Core unit tests.** These need only CMake and a C++20 compiler; no engine is required. They also run
in GitHub Actions (`.github/workflows/core-tests.yml`).

```sh
cmake -S Tests -B Intermediate/CoreTests
cmake --build Intermediate/CoreTests
ctest --test-dir Intermediate/CoreTests --output-on-failure
```

The 49 tests cover most of the rule-level items in spec §19:

* timing windows for every difficulty
* Perfect/Good/Miss, auto-miss, and early-press misses
* energy gain/loss, the energy cap and game over
* per-lane combos and multiplier tiers for all four difficulties
* capture: activation after 4 hits, energy cost, auto-play, 30 s expiry, the fresh streak rule and affordability
* mute after 4 misses, and recovery with one hit
* all six powerups: stacking, Fever ramp and break, Shield, Slow Motion rate, Lane Cleaner choice and
  timeout, Auto-Capture targeting, pickup and despawn, spawn schedule and weights
* scoring, the completion bonus and the end-of-song summary
* chart density scaling and chord expansion
* clock slewing and resync
* WAV decoding for every format
* mixer crossfades, rate, lead-in and solo
* SFX bounds

**In-game console commands.** Open the console with the backtick/tilde key:

| Command | Purpose |
|---|---|
| `amp.AutoPlay 1` | Every lane plays itself, so you can check a chart against its audio |
| `amp.GivePowerup fever` | Apply a powerup immediately (`score_2x`, `lane_cleaner`, `slow_motion`, `shield`, `fever`, `auto_capture`) |
| `amp.SoloLane 3` | Hear only one instrument track (spec §10.3.1 solo). `0` restores the full mix |

Turn on *Settings → Gameplay → Performance Stats* to show FPS, audio sync drift, resync count and input latency (spec §11.2.4).

## Interpretation notes

The specification is ambiguous or self-contradictory in places. These are the decisions made, all in
`Core/` so they are easy to change:

| Topic | Decision |
|---|---|
| Lane keys | A lane key both moves the ship and fires at that lane (§2.3, §2.5, §8.3). |
| Inputs outside the window (§19.3) | A press up to 100 ms before the Good window (the "early miss window") misses the upcoming note. Presses further away are free *ghost presses*, so lane hopping is not punished. |
| Which note a press hits | The pending note closest to the press inside the Good window. |
| Energy | Table §15 and the §14.1 JSON are used: Good = +1 on every difficulty, Miss = −1/−3/−4/−4. §7.1.1 says +2 for a Mellow Good, which conflicts. |
| Combo tiers | Table §15 thresholds. The multiplier uses the combo *after* the hit (the 5th hit on Normal scores 1.2x). |
| Capture | 4 consecutive **player** hits capture the lane for 30 s of song time. Auto-played notes are Perfects: they score, give energy and keep the combo, but do not build a new streak. A capture needs *more* energy than its cost (5), so it can never end the run; the streak keeps counting until the capture is affordable. Songs can tune all of this via `rules`. |
| Score 2x | Each pickup is an independent 15 s timer, and overlapping pickups stack multiplicatively (2x, 4x, ...). This satisfies both "extend duration" and "2x × 2x = 4x". |
| Fever | Starts at 1.5x and gains +0.1x per hit (player or auto), capped at 3.0x. Any miss breaks it. It multiplies with Score 2x. |
| Shield | Absorbs the energy loss of the next miss. The miss still breaks the combo and counts towards muting. |
| Lane Cleaner | After pickup, the next lane key chooses the lane (3 s, then the most crowded lane is chosen automatically). It clears the notes currently on screen in that lane, with no score or energy change. |
| Slow Motion | Music and notes run at 0.5x through resampling (an octave lower) for 10 s of real time. Timing windows stay the same in *song* milliseconds. |
| Auto-Capture | Targets the uncaptured lane that still has notes coming, with the most consecutive misses, then the lowest combo. It follows the normal capture energy rule and fizzles when unaffordable. |
| Powerup odds | Normal uses §6.3.3 (Score 2x 25%, others 15%). Brutal and Insane use table §15 where §6.3.3 conflicts: Brutal has Fever 10%, Lane Cleaner 20%, Score 2x 20%; Insane has Shield and Auto-Capture 8%, Score 2x 25%, Fever 15%. |
| Powerup timing | One spawns every 10/10/8/6 s (table §15) ± 20% (Normal 8–12 s, §6.3.1) in a random lane. It falls for 8 s, top to bottom, and is picked up while it overlaps the ship. |
| Note speed | `note_speed` is read as reference units per second with the hit line 1000 units away, so approach time = 1000 / speed: Normal 2.0 s, Insane 1.1 s. `approach_time_ms` overrides it. |
| Accuracy | (Perfect + Auto + ½ Good) / judged notes. The spec's sample numbers are not self-consistent. |
| Sync | The play head is sample-accurate, so no beat detection is needed. BPM and beat markers drive the beat-synced visuals. A 3 s silent lead-in precedes song time 0. |
| Audio offset | Positive values compensate for audio that is heard late. It is added to the song's `offset_ms`. |
| Leaderboards | Game-over runs are recorded too, marked "(KO)". |
| Extras | Restart in the pause menu, a 3-2-1 countdown on resume, auto-pause when the window loses focus, and colourblind palettes (Okabe–Ito based). |

## Known limitations

* Runtime audio decoding is WAV only (see above). Imported `USoundWave` assets are not used, so songs stay drop-in files.
* There is no chart editor and no replays (out of MVP scope, §23). All input is timestamped, so replays could be added.
* Leaderboards are local only.
