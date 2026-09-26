#pragma once

#include "CoreMinimal.h"
#include "Core/AmpSimulation.h"
#include "Core/AmpSongClock.h"
#include "Core/AmpStemMixer.h"

#include <memory>
#include <vector>

class UAmplitudeSfxComponent;
class UAmplitudeStemPlayerComponent;
struct FAmplitudeSongDefinition;

struct FAmplitudeSessionConfig
{
	TSharedPtr<const FAmplitudeSongDefinition> Song;
	Amp::EDifficulty Difficulty = Amp::EDifficulty::Normal;
	/** May be null: the song then plays silently on the wall clock. */
	std::shared_ptr<const Amp::FSongAudio> Audio;
	/** Player's global audio offset (the song's own offset is added on top). */
	double UserOffsetMs = 0.0;
	/** Silent run-up before song time 0 so the first notes can scroll in. */
	double LeadInMs = 3000.0;
	uint64 Seed = 1;
	bool bAutoPlay = false;
};

/** Final results of a run, shown on the results screen. */
struct FAmplitudeRunResult
{
	TSharedPtr<const FAmplitudeSongDefinition> Song;
	Amp::EDifficulty Difficulty = Amp::EDifficulty::Normal;
	Amp::FRunSummary Summary;
	int32 LeaderboardRank = INDEX_NONE;
	int64 PreviousBest = 0;
	bool bHadPreviousBest = false;
	bool bNewRecord = false;
};

/**
 * One play-through of a song (the game loop of spec 13.1): buffers input, keeps the song clock
 * slaved to the audio play head, advances the simulation and turns its events into sound.
 */
class AMPLITUDE_API FAmplitudeSession
{
public:
	FAmplitudeSession(const FAmplitudeSessionConfig& InConfig, UAmplitudeStemPlayerComponent* InStemPlayer, UAmplitudeSfxComponent* InSfxPlayer);
	~FAmplitudeSession();

	void Start(double WallSeconds);
	void Tick(double WallSeconds);

	/** Records a lane press; it is judged at the song time it happened, on the next Tick. */
	void QueuePress(int32 Lane, double WallSeconds);
	void SetPaused(bool bPause, double WallSeconds);
	void StopAudio();

	void SetAutoPlay(bool bEnabled);
	void GrantPowerup(Amp::EPowerupType Type);

	bool IsPaused() const { return bPaused; }
	bool IsFinished() const { return Simulation.IsFinished(); }
	const Amp::FSimulation& GetSimulation() const { return Simulation; }
	const FAmplitudeSongDefinition& GetSong() const { return *Config.Song; }
	TSharedPtr<const FAmplitudeSongDefinition> GetSongPtr() const { return Config.Song; }
	Amp::EDifficulty GetDifficulty() const { return Config.Difficulty; }
	bool HasAudio() const { return Config.Audio != nullptr; }

	double GetSongTimeMs() const { return Clock.GetTimeMs(); }
	/** Song length shown on the HUD timer. */
	double GetDisplayDurationMs() const;
	/** 0 right on a beat, rising to 1 just before the next one. */
	double GetBeatPhase() const;

	double GetAudioDriftMs() const { return Clock.GetLastDriftMs(); }
	int32 GetResyncCount() const { return Clock.GetResyncCount(); }
	double GetInputLatencyMs() const { return InputLatencyMs; }

	/** Receives every simulation event after audio has reacted to it (used for visual effects). */
	TFunction<void(const Amp::FEvent&)> OnEvent;

private:
	struct FQueuedPress
	{
		int32 Lane = 0;
		double WallSeconds = 0.0;
	};

	double GetTotalOffsetMs() const;
	void SyncToAudio(double WallSeconds);
	void HandleEvent(const Amp::FEvent& Event);
	void PlaySfx(Amp::ESfx Sfx) const;

	FAmplitudeSessionConfig Config;
	TWeakObjectPtr<UAmplitudeStemPlayerComponent> StemPlayer;
	TWeakObjectPtr<UAmplitudeSfxComponent> SfxPlayer;

	Amp::FSimulation Simulation;
	Amp::FSongClock Clock;
	TArray<FQueuedPress> PendingPresses;
	std::vector<Amp::FEvent> EventScratch;

	double LastWallSeconds = 0.0;
	double AppliedRate = 1.0;
	double InputLatencyMs = 0.0;
	uint64 SerialAtResume = 0;
	bool bPaused = false;
};
