#pragma once

#include "Core/AmpTypes.h"

namespace Amp
{
	/** Difficulty tuning from spec sections 7 and 15. */
	FDifficultyParams GetDefaultDifficultyParams(EDifficulty Difficulty);

	/** Converts a note speed (reference units per second, hit line 1000 units away) into an approach time. */
	double ApproachTimeFromSpeed(double NoteSpeed);

	/** Lane combo multiplier for the given combo count (section 3.2.2 / table 15). */
	double GetComboMultiplier(const FDifficultyParams& Params, int32_t Combo);

	/** Perfect or Good for an absolute timing error inside the good window, None otherwise. */
	EJudgement JudgeTimingError(const FDifficultyParams& Params, double AbsErrorMs);

	/** Score = Base x Lane Combo Multiplier x Global Multiplier, rounded to the nearest point. */
	int64_t ComputeHitScore(int32_t BasePoints, double ComboMultiplier, double GlobalMultiplier);

	/** Accuracy in percent: Perfect (including auto-played) = 100%, Good = 50%, Miss = 0%. */
	double ComputeAccuracyPercent(int32_t Perfect, int32_t Good, int32_t Miss);
}
