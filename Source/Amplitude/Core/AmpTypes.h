// Amplitude - engine-agnostic gameplay core.
//
// Everything under Source/Amplitude/Core is plain C++20 with no Unreal dependencies so the
// rules can be unit tested outside the engine (see Tests/). The Unreal layer wraps these types.

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace Amp
{
	constexpr int32_t NumLanes = 6;
	constexpr int32_t NumDifficulties = 4;
	constexpr int32_t NumPowerupTypes = 6;
	constexpr int32_t NumComboTiers = 4;

	enum class ENoteType : uint8_t
	{
		Single,
		Double,
		Triple,
		Stream
	};

	enum class EJudgement : uint8_t
	{
		None,
		Perfect,
		Good,
		Miss
	};

	enum class EDifficulty : uint8_t
	{
		Mellow,
		Normal,
		Brutal,
		Insane
	};

	enum class EPowerupType : uint8_t
	{
		Score2x,
		LaneCleaner,
		SlowMotion,
		Shield,
		Fever,
		AutoCapture
	};

	/** One entry of a song chart: a moment in time that may cover several lanes (double/triple notes). */
	struct FChartEntry
	{
		int32_t Id = 0;
		double TimeMs = 0.0;
		/** Bit i set => lane i (0-based) has a note at this time. */
		uint8_t LaneMask = 0;
		ENoteType Type = ENoteType::Single;
	};

	/** A single playable note in one lane. Multi-lane chart entries expand to one FNote per lane. */
	struct FNote
	{
		int32_t Id = 0;
		/** Index of the chart entry this note came from; notes of the same chord share it. */
		int32_t ChordId = 0;
		double TimeMs = 0.0;
		/** 0-based lane index (lane 1 in the UI is index 0). */
		int32_t Lane = 0;
		ENoteType Type = ENoteType::Single;

		EJudgement Judgement = EJudgement::None;
		bool bAutoPlayed = false;
		bool bCleared = false;
		/** Input time minus note time, in song milliseconds (negative = early). */
		double HitOffsetMs = 0.0;
		/** Song time at which the note was judged or cleared. */
		double ResolvedAtMs = 0.0;

		bool IsPending() const { return Judgement == EJudgement::None && !bCleared; }
	};

	/** Per-difficulty tuning. Defaults come from GetDefaultDifficultyParams, songs may override. */
	struct FDifficultyParams
	{
		/** Note count relative to the Normal chart (0.4 = 40%). */
		double NoteDensity = 1.0;
		/** Descent speed in reference units per second (the hit line is 1000 units below the spawn point). */
		double NoteSpeed = 500.0;
		/** How long a note is visible before it reaches the hit line. Derived from NoteSpeed. */
		double ApproachTimeMs = 2000.0;

		double PerfectWindowMs = 100.0;
		double GoodWindowMs = 300.0;
		/** Presses this early (but outside the good window) count as a miss of the upcoming note. */
		double EarlyMissWindowMs = 400.0;

		int32_t EnergyOnPerfect = 2;
		int32_t EnergyOnGood = 1;
		int32_t EnergyOnMiss = -3;

		/** Lane combo needed for each multiplier tier; a value <= 0 means the tier is never reached. */
		std::array<int32_t, NumComboTiers> ComboThresholds{5, 10, 20, 50};
		std::array<double, NumComboTiers> ComboMultipliers{1.2, 1.5, 2.0, 3.0};

		double PowerupIntervalMs = 10000.0;
		double PowerupIntervalJitterMs = 2000.0;
		/** Relative spawn weights indexed by EPowerupType. */
		std::array<double, NumPowerupTypes> PowerupWeights{1.0, 1.0, 1.0, 1.0, 1.0, 1.0};

		/** Fraction of single notes turned into double notes when a chart is generated for this difficulty. */
		double ExtraChordRatio = 0.0;
	};

	/** Rules shared by every difficulty (spec sections 3-6, 9). */
	struct FGameRules
	{
		int32_t StartingEnergy = 50;
		int32_t MaxEnergy = 100;
		int32_t LowEnergyThreshold = 20;

		int32_t PerfectPoints = 10;
		int32_t GoodPoints = 5;
		int32_t PowerupCollectPoints = 500;
		int32_t SongCompleteBonus = 1000;

		int32_t CaptureStreak = 4;
		double CaptureDurationMs = 30000.0;
		int32_t CaptureEnergyCost = 5;

		int32_t MuteMissStreak = 4;

		/** Normalised playfield layout: 0 = spawn line at the top, 1 = bottom of the playfield. */
		double HitLineY = 0.82;
		double ShipY = 0.9;

		double PowerupLifetimeMs = 8000.0;
		/** Half-height of the ship's pickup band in normalised playfield units. */
		double PowerupCollectBand = 0.045;
		/** No powerups spawn during the last few seconds of a song. */
		double PowerupSpawnTailMs = 3000.0;

		double Score2xDurationMs = 15000.0;
		double SlowMotionDurationMs = 10000.0;
		double SlowMotionRate = 0.5;
		double ShieldDurationMs = 30000.0;
		double FeverDurationMs = 20000.0;
		double FeverStartMultiplier = 1.5;
		double FeverStepPerHit = 0.1;
		double FeverMaxMultiplier = 3.0;
		double LaneCleanerSelectTimeoutMs = 3000.0;
	};

	const char* GetLaneInstrumentName(int32_t Lane);
	const char* GetLaneInstrumentKey(int32_t Lane);
	const char* GetDifficultyName(EDifficulty Difficulty);
	const char* GetDifficultyKey(EDifficulty Difficulty);
	const char* GetPowerupName(EPowerupType Type);
	const char* GetPowerupKey(EPowerupType Type);
	const char* GetNoteTypeKey(ENoteType Type);

	/** Returns -1 when the key is unknown. Keys are the lower-case identifiers used in song JSON. */
	int32_t FindLaneByInstrumentKey(const char* Key);
	int32_t FindDifficultyByKey(const char* Key);
	int32_t FindPowerupByKey(const char* Key);
	int32_t FindNoteTypeByKey(const char* Key);
}
