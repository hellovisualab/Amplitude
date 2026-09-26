#include "Game/AmplitudeSession.h"

#include "Audio/AmplitudeSfxComponent.h"
#include "Audio/AmplitudeStemPlayerComponent.h"
#include "Core/AmpSfxSynth.h"
#include "Data/AmplitudeSongLibrary.h"

namespace
{
	/** Spec 8.3: input is buffered for up to ~5 frames; anything older is dropped. */
	constexpr double MaxPressAgeSeconds = 5.0 / 60.0 + 0.05;
	/** Audio snapshots older than this mean the audio thread stalled or there is no audio device. */
	constexpr double MaxSnapshotAgeSeconds = 0.25;
	/** Frame hitches are clamped so powerup timers do not jump. */
	constexpr double MaxRealDeltaMs = 250.0;
}

FAmplitudeSession::FAmplitudeSession(const FAmplitudeSessionConfig& InConfig, UAmplitudeStemPlayerComponent* InStemPlayer, UAmplitudeSfxComponent* InSfxPlayer)
	: Config(InConfig)
	, StemPlayer(InStemPlayer)
	, SfxPlayer(InSfxPlayer)
{
	check(Config.Song.IsValid());
}

FAmplitudeSession::~FAmplitudeSession()
{
	StopAudio();
}

double FAmplitudeSession::GetTotalOffsetMs() const
{
	return Config.UserOffsetMs + Config.Song->OffsetMs;
}

void FAmplitudeSession::Start(double WallSeconds)
{
	const FAmplitudeSongDefinition& Song = *Config.Song;
	const int32 DifficultyIndex = static_cast<int32>(Config.Difficulty);
	const double AudioLengthMs = Config.Audio ? Config.Audio->GetLengthMs() : 0.0;
	const double LengthMs = FMath::Max(Song.DurationMs, AudioLengthMs);

	Simulation.Start(Song.BuildNotes(Config.Difficulty), Song.DifficultyParams[DifficultyIndex], Song.Rules, LengthMs, Config.Seed, -Config.LeadInMs);
	Simulation.SetAutoPlayAll(Config.bAutoPlay);

	Clock.Reset(-Config.LeadInMs, WallSeconds, 1.0);
	LastWallSeconds = WallSeconds;
	AppliedRate = 1.0;
	bPaused = false;
	PendingPresses.Reset();

	if (UAmplitudeStemPlayerComponent* Player = StemPlayer.Get())
	{
		// Song time = audio time - offset, so the play head starts at lead-in + offset.
		Player->LoadSong(Config.Audio, -Config.LeadInMs + GetTotalOffsetMs());
		Player->SetPlaying(true);
		SerialAtResume = Player->GetAudioPosition().Serial;
	}
}

void FAmplitudeSession::Tick(double WallSeconds)
{
	if (bPaused)
	{
		return;
	}

	const double RealDeltaMs = FMath::Clamp((WallSeconds - LastWallSeconds) * 1000.0, 0.0, MaxRealDeltaMs);
	LastWallSeconds = WallSeconds;

	// 1-2: advance the smooth clock, then correct it against the audio play head.
	Clock.Advance(WallSeconds, AppliedRate);
	SyncToAudio(WallSeconds);

	// Buffered input: judge each press at the song time it was made, in order.
	PendingPresses.Sort([](const FQueuedPress& A, const FQueuedPress& B) { return A.WallSeconds < B.WallSeconds; });
	for (const FQueuedPress& Press : PendingPresses)
	{
		if (WallSeconds - Press.WallSeconds > MaxPressAgeSeconds)
		{
			continue;
		}
		Simulation.PressLane(Press.Lane, Clock.TimeAtWall(Press.WallSeconds));
		const double LatencyMs = (WallSeconds - Press.WallSeconds) * 1000.0;
		InputLatencyMs = InputLatencyMs <= 0.0 ? LatencyMs : FMath::Lerp(InputLatencyMs, LatencyMs, 0.2);
	}
	PendingPresses.Reset();

	// 3-8: spawning, note progression, auto-miss, capture, powerups and energy all live in the simulation.
	Simulation.Advance(Clock.GetTimeMs(), RealDeltaMs);

	const double Rate = Simulation.GetPlaybackRate();
	if (Rate != AppliedRate)
	{
		AppliedRate = Rate;
		if (UAmplitudeStemPlayerComponent* Player = StemPlayer.Get())
		{
			Player->SetPlaybackRate(Rate);
		}
	}

	EventScratch.clear();
	Simulation.DrainEvents(EventScratch);
	for (const Amp::FEvent& Event : EventScratch)
	{
		HandleEvent(Event);
	}
}

void FAmplitudeSession::QueuePress(int32 Lane, double WallSeconds)
{
	if (!bPaused && !Simulation.IsFinished())
	{
		PendingPresses.Add({Lane, WallSeconds});
	}
}

void FAmplitudeSession::SetPaused(bool bPause, double WallSeconds)
{
	if (bPause == bPaused)
	{
		return;
	}
	bPaused = bPause;
	PendingPresses.Reset();

	UAmplitudeStemPlayerComponent* Player = StemPlayer.Get();
	if (Player != nullptr)
	{
		Player->SetPlaying(!bPause && !Simulation.IsFinished());
	}
	if (!bPause)
	{
		// Paused time must not count as song time; wait for a fresh audio snapshot before syncing again.
		Clock.Rebase(WallSeconds);
		LastWallSeconds = WallSeconds;
		SerialAtResume = Player != nullptr ? Player->GetAudioPosition().Serial : 0;
	}
}

void FAmplitudeSession::StopAudio()
{
	if (UAmplitudeStemPlayerComponent* Player = StemPlayer.Get())
	{
		Player->SetPlaying(false);
	}
}

void FAmplitudeSession::SetAutoPlay(bool bEnabled)
{
	Config.bAutoPlay = bEnabled;
	Simulation.SetAutoPlayAll(bEnabled);
}

void FAmplitudeSession::GrantPowerup(Amp::EPowerupType Type)
{
	Simulation.ApplyPowerup(Type);
}

double FAmplitudeSession::GetDisplayDurationMs() const
{
	return FMath::Max(Config.Song->GetDisplayDurationMs(), Config.Audio ? Config.Audio->GetLengthMs() : 0.0);
}

double FAmplitudeSession::GetBeatPhase() const
{
	const double BeatMs = 60000.0 / FMath::Max(1.0, Config.Song->Bpm);
	const double Beats = (Clock.GetTimeMs() - Config.Song->GetFirstBeatMs()) / BeatMs;
	return Beats - FMath::FloorToDouble(Beats);
}

void FAmplitudeSession::SyncToAudio(double WallSeconds)
{
	const UAmplitudeStemPlayerComponent* Player = StemPlayer.Get();
	if (Player == nullptr)
	{
		return;
	}

	const FAmplitudeAudioPosition Position = Player->GetAudioPosition();
	if (!Position.bPlaying || Position.Serial <= SerialAtResume)
	{
		return;
	}
	const double AgeSeconds = WallSeconds - Position.RenderedAtSeconds;
	if (AgeSeconds < 0.0 || AgeSeconds > MaxSnapshotAgeSeconds)
	{
		return;
	}

	// The buffer that started at PositionMs is heard roughly one buffer after it was rendered.
	const double HeardMs = Position.PositionMs + (AgeSeconds * 1000.0 - Position.BufferMs) * Position.Rate;
	Clock.Sync(HeardMs - GetTotalOffsetMs());
}

void FAmplitudeSession::PlaySfx(Amp::ESfx Sfx) const
{
	if (UAmplitudeSfxComponent* Player = SfxPlayer.Get())
	{
		Player->Play(Sfx);
	}
}

void FAmplitudeSession::HandleEvent(const Amp::FEvent& Event)
{
	UAmplitudeStemPlayerComponent* Player = StemPlayer.Get();
	switch (Event.Type)
	{
	case Amp::EEventType::NoteHit:
		// Auto-played notes are carried by the music itself; only player hits get a feedback sound.
		if (!Event.bAuto)
		{
			PlaySfx(Event.Judgement == Amp::EJudgement::Perfect ? Amp::ESfx::Perfect : Amp::ESfx::Good);
		}
		break;
	case Amp::EEventType::NoteMissed:
		PlaySfx(Amp::ESfx::Miss);
		break;
	case Amp::EEventType::LaneCaptured:
		PlaySfx(Amp::ESfx::Capture);
		break;
	case Amp::EEventType::LaneMuted:
		if (Player != nullptr)
		{
			Player->SetLaneMuted(Event.Lane, true);
		}
		break;
	case Amp::EEventType::LaneUnmuted:
		if (Player != nullptr)
		{
			Player->SetLaneMuted(Event.Lane, false);
		}
		break;
	case Amp::EEventType::PowerupCollected:
		PlaySfx(Amp::ESfx::Powerup);
		break;
	case Amp::EEventType::ShieldAbsorbedMiss:
		PlaySfx(Amp::ESfx::ShieldBreak);
		break;
	case Amp::EEventType::LaneCleared:
		PlaySfx(Amp::ESfx::LaneClear);
		break;
	case Amp::EEventType::GameOver:
		PlaySfx(Amp::ESfx::GameOver);
		StopAudio();
		break;
	case Amp::EEventType::SongComplete:
		PlaySfx(Amp::ESfx::SongComplete);
		break;
	default:
		break;
	}

	if (OnEvent)
	{
		OnEvent(Event);
	}
}
