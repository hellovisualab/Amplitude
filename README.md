# Amplitude

A lane-based rhythm-action game for PC (Windows/Linux) built with **Unreal Engine 5** (5.4 or newer; the project is associated with 5.8) in C++,
implementing the *Amplitude: Rhythm-Action Game – Complete UE5 Implementation Specification v1.0*.

The player flies the **Beat Blaster** down a winding 3D track of six instrument lanes (Drums, Synth,
Bass, Vocals, Pad, FX), steering between lanes and firing its three gem buttons in time with a
multitrack song, like the original *Amplitude*. The song **builds up as you play it**: at the start
nothing is heard; an instrument comes in while you hit its gems and drops out when you miss or leave
its lane, and a captured lane keeps playing by itself while you move on to the next one. Energy,
per-lane combos, lane capture, muting and six powerups complete the rules.

The whole game is code: the 3D stage is built from engine shapes and procedural meshes, the HUD and
menus are Slate, sound effects are synthesised in real time, and songs are loaded at runtime from
JSON + WAV. The project has **no binary content assets** and runs on the engine's built-in `Entry` map.
The look is dark and modern throughout: near-black skies with stars and a pale moon that shift tone
with each section of the song, glowing gems and lane edges, soft fog, and scenery that pulses harder
the more of the song is playing. Menus and HUD use the same dark style.

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

The Beat Blaster sits in one lane at a time. Steer it left and right between lanes, and fire the
**left / middle / right** gem buttons as the gems of your lane reach the hit line. Press two or three
buttons together for chords. Gems in the other lanes pass by harmlessly.

| Action | Primary | Alternate | Gamepad |
|---|---|---|---|
| Move left | A | ← | D-pad Left (or left stick) |
| Move right | D | → | D-pad Right (or left stick) |
| Left gem | J | Z | LB / L1 |
| Middle gem | K | X | RB / R1 |
| Right gem | L | C | RT / R2 |
| Pause | Esc | P | Start |

In the menus, use the arrow keys, D-pad or mouse to navigate. Enter, Space or A/Cross confirms;
Esc, Backspace or B/Circle goes back. All gameplay bindings can be remapped in *Settings → Controls*.
Remapping supports several saved profiles and applies instantly.

## Project layout

```
Amplitude.uproject          UE 5 project (EnhancedInput and ProceduralMeshComponent plugins)
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
  Stage/                    The 3D presentation: AAmplitudeStage (track, gems, ship, camera, lights, sky,
                            scenery, sparks) and the materials it generates in code
  UI/                       Slate UI: HUD over the 3D view, menus, settings, shared widgets and style
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
                                 │    └─ Amp::FSimulation  ◀── timestamped moves and gem presses
                                 ├─ AAmplitudeStage        3D view: reads the session every frame, camera
                                 └─ Slate root: SAmplitudeGameView (HUD) + current menu screen
AAmplitudePlayerController: Enhanced Input actions/mapping context built at runtime from the control profile
```

Each frame follows the order in spec §13.1. The session advances the clock and syncs it to the
audio play head. It then judges buffered presses at the song time they were made, and advances the
simulation (auto-play, auto-miss, captures, powerups, energy, completion). Finally it turns the
simulation's events into sound and visual effects.

**Timing.** The stem mixer publishes its sample-accurate play head after every audio buffer. The
song clock advances smoothly with wall time × playback rate and slews towards the audio position.
If the drift exceeds 50 ms, the clock snaps to the audio position (spec §10.2 and §13.3). Moves and
gem presses are timestamped when Slate receives the key, not when Enhanced Input fires later in the
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
             "mute_miss_streak": 4, "starting_energy": 50,
             "auto_advance_on_capture": true, "powerup_collect_window_ms": 250,
             "idle_lane_gain": 0.0 },         // optional, defaults per spec
  "notes": [                            // base chart, authored at Normal density
    { "id": 1, "time_ms": 1000, "lane": 1, "column": 2 },
    { "id": 2, "time_ms": 2000, "lane": "bass", "column": "left" },
    { "id": 3, "time_ms": 3000, "lane": 1, "column": [1, 2], "type": "double" },
    { "id": 4, "time_ms": 4000, "lane": 2, "column": 3, "type": "stream", "count": 4, "interval_ms": 115 }
  ],
  "notes_brutal": [ ... ],              // optional hand-authored charts per difficulty (used as-is)
  "events": { "beat_markers": [ { "time_ms": 0, "beat": 0 } ] }
}
```

* **Lanes** are 1-6, or instrument names (`"drums"`, and so on).
* **Columns** are the three gem buttons: 1-3 or `"left"` / `"middle"` / `"right"`. An array makes a
  chord inside the lane. Charts without columns still load: their gems walk middle, left, middle, right,
  and an old multi-lane array such as `"lane": [1, 4]` becomes a two-gem chord in the first lane.
* **Difficulty charts.** If `notes_<difficulty>` exists, it is used exactly as written. Otherwise the
  base chart (`notes`, or the nearest authored chart) is scaled by `note_density_multiplier`:
  * Mellow (40%) keeps an evenly spread subset.
  * Brutal (150%) and Insane (200%) subdivide gaps between notes of the same lane (never closer than
    150 ms, and silent stretches longer than 1.2 s stay silent). Added gems walk across the columns.
  * Insane also turns 1 in 8 single notes into two-button chords.
* **Stems.** Because the song builds up instrument by instrument, every lane needs its own audio: one
  channel of a multichannel WAV per instrument, or one stem file per instrument (`"stems"`). A song
  whose instruments are all mixed into one stereo file would play all or nothing.
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

The 56 tests cover most of the rule-level items in spec §19:

* timing windows for every difficulty
* steering between lanes, the lane history, and misses versus free skips in other lanes
* the music build-up: which instruments are heard as lanes are played, missed, skipped and captured
* Perfect/Good/Miss, wrong buttons, auto-miss, early-press misses and chords
* energy gain/loss, the energy cap and game over
* per-lane combos and multiplier tiers for all four difficulties
* capture: activation after 4 hits, energy cost, auto-play, auto-advance to the next lane, 30 s expiry, the fresh streak rule and affordability
* mute after 4 misses, and recovery with one hit
* all six powerups: stacking, Fever ramp and break, Shield, Slow Motion rate, Lane Cleaner choice and
  timeout, Auto-Capture targeting, pickup and despawn, spawn schedule and weights
* scoring, the completion bonus and the end-of-song summary
* column parsing helpers, per-lane chart density scaling and chord expansion
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
| `amp.Exposure 0.6` | Brightness (exposure compensation in stops) of the 3D stage |
| `amp.SaveMaterials` | Editor only: saves the stage materials to `Content/Amplitude/Materials` (see below) |

**Materials.** The stage builds three simple materials in code (lit, unlit and additive, each with
`Color` and `Glow` parameters). In the editor they are generated the first time you press Play. To
package the game with them, press Play once, stop, and type `amp.SaveMaterials` in the editor console
(*Window → Output Log*, or the backtick key); they are saved to `Content/Amplitude/Materials` and
cooked with the game. Without them a packaged build falls back to the engine's flat basic-shape
material.

Turn on *Settings → Gameplay → Performance Stats* to show FPS, audio sync drift, resync count and input latency (spec §11.2.4).

## Interpretation notes

The specification is ambiguous or self-contradictory in places. These are the decisions made, all in
`Core/` so they are easy to change:

| Topic | Decision |
|---|---|
| Controls | As in the original *Amplitude* (not the spec's six lane keys): left/right steer the Beat Blaster between lanes, three buttons fire at the left/middle/right gem columns of its lane. Charts carry a column per gem. |
| Music build-up | Like the original game, an instrument is only heard while its lane is captured or while the player keeps hitting its gems; a miss or a skipped gem drops it out (20 ms fade in, 150 ms fade out). `idle_lane_gain` lets a song keep a quiet bed of the other instruments (default 0, silent). The spec's "mute after 4 misses" is kept as a lane status. |
| Other lanes | Only the lane the ship is in when a gem reaches the hit line counts. Gems of other lanes are *skipped*: no score, no energy loss, no combo break, but they do reset that lane's capture streak. A gem that already passed can still be hit if you arrive within its Good window. |
| Inputs outside the window (§19.3) | A wrong button while a gem of your lane is inside the Good window loses that gem. A press up to 100 ms before the Good window misses the next gem of that column. Anything else is a free *ghost press*. |
| Which note a press hits | The pending gem of that column closest to the press inside the Good window. |
| Energy | Table §15 and the §14.1 JSON are used: Good = +1 on every difficulty, Miss = −1/−3/−4/−4. §7.1.1 says +2 for a Mellow Good, which conflicts. |
| Combo tiers | Table §15 thresholds. The multiplier uses the combo *after* the hit (the 5th hit on Normal scores 1.2x). |
| Capture | 4 consecutive **player** hits capture the lane for 30 s of song time, and the Beat Blaster then jumps to the nearest free lane with music coming (`auto_advance_on_capture`). The demo song uses a full two-bar phrase (8 hits) to capture, closer to the original game. Auto-played notes are Perfects: they score, give energy and keep the combo, but do not build a new streak. A capture needs *more* energy than its cost (5), so it can never end the run; the streak keeps counting until the capture is affordable. Songs can tune all of this via `rules`. |
| Score 2x | Each pickup is an independent 15 s timer, and overlapping pickups stack multiplicatively (2x, 4x, ...). This satisfies both "extend duration" and "2x × 2x = 4x". |
| Fever | Starts at 1.5x and gains +0.1x per hit (player or auto), capped at 3.0x. Any miss breaks it. It multiplies with Score 2x. |
| Shield | Absorbs the energy loss of the next miss. The miss still breaks the combo and counts towards muting. |
| Lane Cleaner | After pickup, steer to the lane you want and press any gem button (3 s, then the most crowded lane is chosen automatically). It clears the notes currently on screen in that lane, with no score or energy change. |
| Slow Motion | Music and notes run at 0.5x through resampling (an octave lower) for 10 s of real time. Timing windows stay the same in *song* milliseconds. |
| Auto-Capture | Targets the uncaptured lane that still has notes coming, with the most consecutive misses, then the lowest combo. It follows the normal capture energy rule and fizzles when unaffordable. |
| Powerup odds | Normal uses §6.3.3 (Score 2x 25%, others 15%). Brutal and Insane use table §15 where §6.3.3 conflicts: Brutal has Fever 10%, Lane Cleaner 20%, Score 2x 20%; Insane has Shield and Auto-Capture 8%, Score 2x 25%, Fever 15%. |
| Powerup timing | One spawns every 10/10/8/6 s (table §15) ± 20% (Normal 8–12 s, §6.3.1) at the far end of a random uncaptured lane. It rides down the lane like a gem and is collected if the Beat Blaster is in that lane within 250 ms of it reaching the hit line. |
| Note speed | `note_speed` is read as reference units per second with the hit line 1000 units away, so approach time = 1000 / speed: Normal 2.0 s, Insane 1.1 s. `approach_time_ms` overrides it. The 3D track is always the same length, so gems simply travel faster. |
| Accuracy | (Perfect + Auto + ½ Good) / judged notes. The spec's sample numbers are not self-consistent. |
| Sync | The play head is sample-accurate, so no beat detection is needed. BPM and beat markers drive the beat-synced visuals. A 3 s silent lead-in precedes song time 0. |
| Audio offset | Positive values compensate for audio that is heard late. It is added to the song's `offset_ms`. |
| Leaderboards | Game-over runs are recorded too, marked "(KO)". |
| Extras | Restart in the pause menu, a 3-2-1 countdown on resume, auto-pause when the window loses focus, and colourblind palettes (Okabe–Ito based). |

## Known limitations

* Runtime audio decoding is WAV only (see above). Imported `USoundWave` assets are not used, so songs stay drop-in files.
* There is no chart editor and no replays (out of MVP scope, §23). All input is timestamped, so replays could be added.
* Leaderboards are local only.
* The stage materials are generated in the editor; packaged builds need `amp.SaveMaterials` first (see *Testing*).
