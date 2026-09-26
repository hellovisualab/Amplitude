#include "Core/AmpRules.h"

#include <cmath>
#include <cstring>

namespace Amp
{
	namespace
	{
		const char* const LaneNames[NumLanes] = {"Drums", "Synth", "Bass", "Vocals", "Pad", "FX"};
		const char* const LaneKeys[NumLanes] = {"drums", "synth", "bass", "vocals", "pad", "fx"};
		const char* const DifficultyNames[NumDifficulties] = {"Mellow", "Normal", "Brutal", "Insane"};
		const char* const DifficultyKeys[NumDifficulties] = {"mellow", "normal", "brutal", "insane"};
		const char* const PowerupNames[NumPowerupTypes] = {"Score 2x", "Lane Cleaner", "Slow Motion", "Shield", "Fever Mode", "Auto-Capture"};
		const char* const PowerupKeys[NumPowerupTypes] = {"score_2x", "lane_cleaner", "slow_motion", "shield", "fever", "auto_capture"};
		const char* const NoteTypeKeys[4] = {"single", "double", "triple", "stream"};

		int32_t FindKey(const char* const* Keys, int32_t Count, const char* Key)
		{
			if (Key == nullptr)
			{
				return -1;
			}
			for (int32_t Index = 0; Index < Count; ++Index)
			{
				if (std::strcmp(Keys[Index], Key) == 0)
				{
					return Index;
				}
			}
			return -1;
		}
	}

	const char* GetLaneInstrumentName(int32_t Lane)
	{
		return (Lane >= 0 && Lane < NumLanes) ? LaneNames[Lane] : "?";
	}

	const char* GetLaneInstrumentKey(int32_t Lane)
	{
		return (Lane >= 0 && Lane < NumLanes) ? LaneKeys[Lane] : "?";
	}

	const char* GetDifficultyName(EDifficulty Difficulty)
	{
		return DifficultyNames[static_cast<int32_t>(Difficulty)];
	}

	const char* GetDifficultyKey(EDifficulty Difficulty)
	{
		return DifficultyKeys[static_cast<int32_t>(Difficulty)];
	}

	const char* GetPowerupName(EPowerupType Type)
	{
		return PowerupNames[static_cast<int32_t>(Type)];
	}

	const char* GetPowerupKey(EPowerupType Type)
	{
		return PowerupKeys[static_cast<int32_t>(Type)];
	}

	const char* GetNoteTypeKey(ENoteType Type)
	{
		return NoteTypeKeys[static_cast<int32_t>(Type)];
	}

	int32_t FindLaneByInstrumentKey(const char* Key)
	{
		return FindKey(LaneKeys, NumLanes, Key);
	}

	int32_t FindDifficultyByKey(const char* Key)
	{
		return FindKey(DifficultyKeys, NumDifficulties, Key);
	}

	int32_t FindPowerupByKey(const char* Key)
	{
		return FindKey(PowerupKeys, NumPowerupTypes, Key);
	}

	int32_t FindNoteTypeByKey(const char* Key)
	{
		return FindKey(NoteTypeKeys, 4, Key);
	}

	double ApproachTimeFromSpeed(double NoteSpeed)
	{
		constexpr double HitLineDistance = 1000.0;
		constexpr double MinSpeed = 50.0;
		return HitLineDistance / (NoteSpeed > MinSpeed ? NoteSpeed : MinSpeed) * 1000.0;
	}

	FDifficultyParams GetDefaultDifficultyParams(EDifficulty Difficulty)
	{
		FDifficultyParams Params;
		switch (Difficulty)
		{
		case EDifficulty::Mellow:
			Params.NoteDensity = 0.4;
			Params.NoteSpeed = 300.0;
			Params.PerfectWindowMs = 150.0;
			Params.GoodWindowMs = 400.0;
			Params.EarlyMissWindowMs = 500.0;
			Params.EnergyOnMiss = -1;
			Params.ComboThresholds = {3, 5, 10, 0};
			Params.PowerupIntervalMs = 10000.0;
			Params.PowerupWeights = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
			break;

		case EDifficulty::Normal:
			Params.NoteDensity = 1.0;
			Params.NoteSpeed = 500.0;
			Params.PerfectWindowMs = 100.0;
			Params.GoodWindowMs = 300.0;
			Params.EarlyMissWindowMs = 400.0;
			Params.EnergyOnMiss = -3;
			Params.ComboThresholds = {5, 10, 20, 50};
			Params.PowerupIntervalMs = 10000.0;
			// Score 2x 25%, every other powerup 15%.
			Params.PowerupWeights = {25.0, 15.0, 15.0, 15.0, 15.0, 15.0};
			break;

		case EDifficulty::Brutal:
			Params.NoteDensity = 1.5;
			Params.NoteSpeed = 700.0;
			Params.PerfectWindowMs = 75.0;
			Params.GoodWindowMs = 250.0;
			Params.EarlyMissWindowMs = 350.0;
			Params.EnergyOnMiss = -4;
			Params.ComboThresholds = {8, 15, 30, 60};
			Params.PowerupIntervalMs = 8000.0;
			// Score 2x 20%, Lane Cleaner 20%, Fever 10%, the remaining three share 50%.
			Params.PowerupWeights = {12.0, 12.0, 10.0, 10.0, 6.0, 10.0};
			break;

		case EDifficulty::Insane:
			Params.NoteDensity = 2.0;
			Params.NoteSpeed = 900.0;
			Params.PerfectWindowMs = 50.0;
			Params.GoodWindowMs = 200.0;
			Params.EarlyMissWindowMs = 300.0;
			Params.EnergyOnMiss = -4;
			Params.ComboThresholds = {12, 20, 50, 100};
			Params.PowerupIntervalMs = 6000.0;
			// Score 2x 25%, Fever 15%, Shield and Auto-Capture 8%, Lane Cleaner and Slow Motion share the rest.
			Params.PowerupWeights = {25.0, 22.0, 22.0, 8.0, 15.0, 8.0};
			Params.ExtraChordRatio = 0.125;
			break;
		}

		Params.PowerupIntervalJitterMs = Params.PowerupIntervalMs * 0.2;
		Params.ApproachTimeMs = ApproachTimeFromSpeed(Params.NoteSpeed);
		return Params;
	}

	double GetComboMultiplier(const FDifficultyParams& Params, int32_t Combo)
	{
		double Multiplier = 1.0;
		for (int32_t Tier = 0; Tier < NumComboTiers; ++Tier)
		{
			const int32_t Threshold = Params.ComboThresholds[static_cast<size_t>(Tier)];
			if (Threshold > 0 && Combo >= Threshold)
			{
				Multiplier = Params.ComboMultipliers[static_cast<size_t>(Tier)];
			}
		}
		return Multiplier;
	}

	EJudgement JudgeTimingError(const FDifficultyParams& Params, double AbsErrorMs)
	{
		if (AbsErrorMs <= Params.PerfectWindowMs)
		{
			return EJudgement::Perfect;
		}
		if (AbsErrorMs <= Params.GoodWindowMs)
		{
			return EJudgement::Good;
		}
		return EJudgement::None;
	}

	int64_t ComputeHitScore(int32_t BasePoints, double ComboMultiplier, double GlobalMultiplier)
	{
		return static_cast<int64_t>(std::llround(static_cast<double>(BasePoints) * ComboMultiplier * GlobalMultiplier));
	}

	double ComputeAccuracyPercent(int32_t Perfect, int32_t Good, int32_t Miss)
	{
		const int32_t Total = Perfect + Good + Miss;
		if (Total <= 0)
		{
			return 0.0;
		}
		return (static_cast<double>(Perfect) + 0.5 * static_cast<double>(Good)) * 100.0 / static_cast<double>(Total);
	}
}
