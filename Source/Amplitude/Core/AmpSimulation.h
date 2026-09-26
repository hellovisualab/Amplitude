#pragma once

#include "Core/AmpRandom.h"
#include "Core/AmpTypes.h"

#include <array>
#include <vector>

namespace Amp
{
	enum class EEventType : uint8_t
	{
		NoteHit,
		NoteMissed,
		GhostPress,
		ShipMoved,
		LaneCaptured,
		CaptureExpired,
		LaneMuted,
		LaneUnmuted,
		PowerupSpawned,
		PowerupCollected,
		PowerupDespawned,
		PowerupEffectEnded,
		LaneCleanerArmed,
		LaneCleared,
		ShieldAbsorbedMiss,
		FeverBroken,
		AutoCaptureFailed,
		EnergyLow,
		GameOver,
		SongComplete
	};

	/** Something that happened inside the simulation; the presentation layer turns these into sound and visuals. */
	struct FEvent
	{
		EEventType Type = EEventType::NoteHit;
		int32_t Lane = -1;
		EJudgement Judgement = EJudgement::None;
		EPowerupType Powerup = EPowerupType::Score2x;
		bool bAuto = false;
		int32_t NoteIndex = -1;
		int32_t PowerupId = -1;
		/** Lane combo after a hit, consecutive misses after a miss, cleared notes, energy cost... */
		int32_t Count = 0;
		int64_t Points = 0;
		/** Total multiplier applied to a hit (combo x global). */
		double Multiplier = 1.0;
		double OffsetMs = 0.0;
		double SongTimeMs = 0.0;
	};

	struct FLaneState
	{
		/** Indices into FSimulation::GetNotes(), in time order. */
		std::vector<int32_t> NoteIndices;
		/** Every note before the cursor has been resolved. */
		size_t Cursor = 0;

		int32_t Combo = 0;
		int32_t MaxCombo = 0;
		/** Consecutive player hits counted towards the next capture. */
		int32_t CaptureStreak = 0;
		bool bCaptured = false;
		double CaptureEndMs = 0.0;
		int32_t ConsecutiveMisses = 0;
		bool bMuted = false;
		int32_t MuteCount = 0;
		int32_t Hits = 0;
		int32_t Misses = 0;
		int32_t Captures = 0;
	};

	struct FFallingPowerup
	{
		int32_t Id = 0;
		EPowerupType Type = EPowerupType::Score2x;
		int32_t Lane = 0;
		double AgeMs = 0.0;
	};

	/** Timers are in real (unpaused) milliseconds so Slow Motion does not stretch its own duration. */
	struct FActiveEffects
	{
		std::vector<double> Score2xRemainingMs;
		double SlowMotionRemainingMs = 0.0;
		double ShieldRemainingMs = 0.0;
		double FeverRemainingMs = 0.0;
		double FeverMultiplier = 1.0;
		/** > 0 while a collected Lane Cleaner waits for the player to pick a lane. */
		double LaneCleanerRemainingMs = 0.0;
	};

	struct FRunStats
	{
		int32_t Perfect = 0;
		int32_t Good = 0;
		int32_t Miss = 0;
		int32_t AutoHits = 0;
		int32_t GhostPresses = 0;
		int32_t NotesCleared = 0;
		int32_t Captures = 0;
		int32_t PowerupsCollected = 0;
		int32_t BestCombo = 0;
	};

	struct FRunSummary
	{
		int64_t Score = 0;
		double AccuracyPercent = 0.0;
		int32_t Perfect = 0;
		int32_t Good = 0;
		int32_t Miss = 0;
		int32_t AutoHits = 0;
		int32_t NotesCleared = 0;
		int32_t TotalNotes = 0;
		int32_t BestCombo = 0;
		int32_t BestLane = -1;
		int32_t BestLaneCombo = 0;
		int32_t WorstLane = -1;
		int32_t WorstLaneMutes = 0;
		int32_t WorstLaneMisses = 0;
		int32_t Captures = 0;
		int32_t PowerupsCollected = 0;
		bool bCompleted = false;
		bool bGameOver = false;
	};

	/**
	 * Deterministic rhythm-game simulation: hit detection, energy, per-lane combos, lane capture,
	 * muting, powerups and scoring. It knows nothing about rendering, audio or wall-clock time;
	 * the caller feeds it song time (driven by the audio clock) and real elapsed time.
	 */
	class FSimulation
	{
	public:
		void Start(std::vector<FNote> InNotes, const FDifficultyParams& InParams, const FGameRules& InRules, double InSongLengthMs, uint64_t Seed, double StartSongTimeMs = 0.0);

		/** Lane key pressed: the Beat Blaster jumps to the lane and fires at its hit zone. */
		void PressLane(int32_t Lane, double SongTimeMs);

		/** Moves the Beat Blaster without firing. */
		void MoveShip(int32_t Lane);

		/** Advances to SongTimeMs: auto-play, auto-miss, captures, powerups and song completion. */
		void Advance(double SongTimeMs, double RealDeltaMs);

		/** Appends pending events to Out and clears the internal queue. */
		void DrainEvents(std::vector<FEvent>& Out);

		void SetPowerupSpawningEnabled(bool bEnabled) { bPowerupSpawning = bEnabled; }
		/** Debug: every lane plays itself (useful to check chart/audio sync). */
		void SetAutoPlayAll(bool bEnabled) { bAutoPlayAll = bEnabled; }
		/** Debug/tests: drops a powerup into a lane immediately. */
		int32_t ForceSpawnPowerup(EPowerupType Type, int32_t Lane);
		/** Applies a powerup's effect as if it had just been collected (no collection points). */
		void ApplyPowerup(EPowerupType Type);

		const std::vector<FNote>& GetNotes() const { return Notes; }
		const FLaneState& GetLane(int32_t Lane) const { return Lanes[static_cast<size_t>(Lane)]; }
		const std::vector<FFallingPowerup>& GetPowerups() const { return Powerups; }
		const FActiveEffects& GetEffects() const { return Effects; }
		const FRunStats& GetStats() const { return Stats; }
		const FDifficultyParams& GetParams() const { return Params; }
		const FGameRules& GetRules() const { return Rules; }

		int64_t GetScore() const { return Score; }
		int32_t GetEnergy() const { return Energy; }
		int32_t GetShipLane() const { return ShipLane; }
		double GetSongTimeMs() const { return SongTimeMs; }
		double GetSongLengthMs() const { return SongLengthMs; }
		int32_t GetPendingNoteCount() const { return PendingNotes; }

		bool IsGameOver() const { return bGameOver; }
		bool IsComplete() const { return bComplete; }
		bool IsFinished() const { return bGameOver || bComplete; }
		bool IsEnergyLow() const { return Energy < Rules.LowEnergyThreshold; }
		bool IsLaneCleanerArmed() const { return Effects.LaneCleanerRemainingMs > 0.0; }
		bool IsShieldActive() const { return Effects.ShieldRemainingMs > 0.0; }
		bool IsFeverActive() const { return Effects.FeverRemainingMs > 0.0; }
		bool IsSlowMotionActive() const { return Effects.SlowMotionRemainingMs > 0.0; }
		int32_t GetScore2xStacks() const { return static_cast<int32_t>(Effects.Score2xRemainingMs.size()); }

		/** Music/note speed factor: 0.5 during Slow Motion, 1 otherwise. */
		double GetPlaybackRate() const;
		/** Product of every active powerup multiplier. */
		double GetGlobalMultiplier() const;
		double GetLaneComboMultiplier(int32_t Lane) const;
		double GetCaptureRemainingMs(int32_t Lane) const;
		/** Normalised vertical position (0 = top, 1 = bottom) of a falling powerup. */
		double GetPowerupY(const FFallingPowerup& Powerup) const;

		FRunSummary Summarize() const;

	private:
		void Emit(const FEvent& Event) { Events.push_back(Event); }
		void AdvanceCursor(FLaneState& Lane);
		void ResolveHit(int32_t NoteIndex, EJudgement Judgement, double OffsetMs, double AtMs, bool bAuto);
		void ResolveMiss(int32_t NoteIndex, double OffsetMs, double AtMs);
		void AddEnergy(int32_t Delta);
		bool TryCapture(int32_t Lane, double AtMs);
		void ProcessDueNotes(double NowMs);
		void UpdateCaptures(double NowMs);
		void UpdateEffects(double RealDeltaMs);
		void UpdatePowerups(double RealDeltaMs, double NowMs);
		void CollectPowerupsAtShip();
		void CollectPowerup(size_t Index);
		void ScheduleNextPowerup();
		void ClearLane(int32_t Lane, double AtMs);
		int32_t PickLaneCleanerTarget() const;
		int32_t PickAutoCaptureLane() const;
		int32_t CountPendingNotes(int32_t Lane, double FromMs, double ToMs) const;
		void CheckCompletion(double NowMs);

		struct FDueNote
		{
			double TimeMs;
			int32_t NoteIndex;
			bool bAutoHit;
		};

		std::vector<FNote> Notes;
		std::array<FLaneState, NumLanes> Lanes;
		std::vector<FFallingPowerup> Powerups;
		std::vector<FEvent> Events;
		std::vector<FDueNote> DueScratch;
		FActiveEffects Effects;
		FRunStats Stats;
		FDifficultyParams Params;
		FGameRules Rules;
		FRandom Random;

		int64_t Score = 0;
		int32_t Energy = 50;
		int32_t ShipLane = 2;
		int32_t PendingNotes = 0;
		int32_t NextPowerupId = 1;
		double SongTimeMs = 0.0;
		double SongLengthMs = 0.0;
		double RealTimeMs = 0.0;
		double NextPowerupAtMs = 0.0;
		bool bGameOver = false;
		bool bComplete = false;
		bool bPowerupSpawning = true;
		bool bAutoPlayAll = false;
	};
}
