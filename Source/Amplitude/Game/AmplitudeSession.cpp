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
	/** An instrument comes in almost instantly on a hit and fades out a little more gently. */
	constexpr double LaneFadeInMs = 20.0;
	constexpr double LaneFadeOutMs = 150.0;
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
	PendingInputs.Reset();

	if (UAmplitudeStemPlayerComponent* Player = StemPlayer.Get())
	{
		// Song time = audio time - offset, so the play head starts at lead-in + offset.
		Player->LoadSong(Config.Audio, -Config.LeadInMs + GetTotalOffsetMs());
		for (float& Gain : AppliedLaneGains)
		{
			Gain = -1.0f;
		}
		UpdateMix(true);
		Player->SetPlaying(true);
		SerialAtResume = Player->GetAudioPosition().Serial;
	}
}

void FAmplitudeSession::UpdateMix(bool bImmediate)
{
	UAmplitudeStemPlayerComponent* Player = StemPlayer.Get();
	if (Player == nullptr)
	{
		return;
	}
	for (int32 Lane = 0; Lane < Amp::NumLanes; ++Lane)
	{
		const float Gain = Simulation.GetLaneMixGain(Lane);
		if (Gain == AppliedLaneGains[Lane])
		{
			continue;
		}
		const double RampMs = bImmediate ? 0.0 : (Gain > AppliedLaneGains[Lane] ? LaneFadeInMs : LaneFadeOutMs);
		AppliedLaneGains[Lane] = Gain;
		Player->SetLaneGain(Lane, Gain, RampMs);
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

	// Buffered input: apply each move and press at the song time it was made, in order.
	PendingInputs.StableSort([](const FQueuedInput& A, const FQueuedInput& B) { return A.WallSeconds < B.WallSeconds; });
	for (const FQueuedInput& Input : PendingInputs)
	{
		if (WallSeconds - Input.WallSeconds > MaxPressAgeSeconds)
		{
			continue;
		}
		const double SongTimeMs = Clock.TimeAtWall(Input.WallSeconds);
		switch (Input.Kind)
		{
		case EInputKind::Step:
			Simulation.StepShip(Input.Value, SongTimeMs);
			break;
		case EInputKind::Jump:
			Simulation.MoveShip(Input.Value, SongTimeMs);
			break;
		case EInputKind::Fire:
		{
			Simulation.PressColumn(Input.Value, SongTimeMs);
			const double LatencyMs = (WallSeconds - Input.WallSeconds) * 1000.0;
			InputLatencyMs = InputLatencyMs <= 0.0 ? LatencyMs : FMath::Lerp(InputLatencyMs, LatencyMs, 0.2);
			break;
		}
		}
	}
	PendingInputs.Reset();

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

	UpdateMix(false);

	EventScratch.clear();
	Simulation.DrainEvents(EventScratch);
	for (const Amp::FEvent& Event : EventScratch)
	{
		HandleEvent(Event);
	}
}

void FAmplitudeSession::QueueInput(EInputKind Kind, int32 Value, double WallSeconds)
{
	if (!bPaused && !Simulation.IsFinished())
	{
		PendingInputs.Add({Kind, Value, WallSeconds});
	}
}

void FAmplitudeSession::QueueStep(int32 Direction, double WallSeconds)
{
	QueueInput(EInputKind::Step, Direction, WallSeconds);
}

void FAmplitudeSession::QueueFire(int32 Column, double WallSeconds)
{
	QueueInput(EInputKind::Fire, Column, WallSeconds);
}

void FAmplitudeSession::QueueJump(int32 Lane, double WallSeconds)
{
	QueueInput(EInputKind::Jump, Lane, WallSeconds);
}

void FAmplitudeSession::SetPaused(bool bPause, double WallSeconds)
{
	if (bPause == bPaused)
	{
		return;
	}
	bPaused = bPause;
	PendingInputs.Reset();

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
	// Instruments are faded in and out by UpdateMix; events only trigger sound effects here.
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
