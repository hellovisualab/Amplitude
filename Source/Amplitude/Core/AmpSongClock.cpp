#include "Core/AmpSongClock.h"

#include <algorithm>
#include <cmath>

namespace Amp
{
	void FSongClock::Reset(double SongTimeMs, double WallSeconds, double InRate)
	{
		TimeMs = SongTimeMs;
		PreviousTimeMs = SongTimeMs;
		LastWallSeconds = WallSeconds;
		Rate = InRate;
		LastDriftMs = 0.0;
		ResyncCount = 0;
	}

	void FSongClock::Advance(double WallSeconds, double InRate)
	{
		const double DeltaSeconds = std::max(0.0, WallSeconds - LastWallSeconds);
		PreviousTimeMs = TimeMs;
		TimeMs += DeltaSeconds * 1000.0 * Rate;
		LastWallSeconds = WallSeconds;
		Rate = InRate;
	}

	void FSongClock::Sync(double AudioSongTimeMs)
	{
		LastDriftMs = AudioSongTimeMs - TimeMs;
		if (std::abs(LastDriftMs) > Config.ResyncThresholdMs)
		{
			TimeMs = AudioSongTimeMs;
			++ResyncCount;
		}
		else
		{
			// Gentle slew; the clock may slow down but never runs backwards between frames.
			TimeMs = std::max(TimeMs + LastDriftMs * Config.SlewFraction, PreviousTimeMs);
		}
	}

	double FSongClock::TimeAtWall(double WallSeconds) const
	{
		return TimeMs + (WallSeconds - LastWallSeconds) * 1000.0 * Rate;
	}
}
