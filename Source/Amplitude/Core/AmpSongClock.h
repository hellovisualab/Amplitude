#pragma once

#include <cstdint>

namespace Amp
{
	/**
	 * Smooth song clock slaved to the audio playback position (spec 13.3).
	 *
	 * The clock advances with wall time x playback rate every frame, then is compared with the
	 * position reported by the audio renderer. Small drift is corrected gradually so notes never
	 * stutter; drift beyond the resync threshold snaps the clock straight to the audio position.
	 */
	class FSongClock
	{
	public:
		struct FConfig
		{
			double ResyncThresholdMs = 50.0;
			/** Fraction of the remaining drift removed per sync. */
			double SlewFraction = 0.1;
		};

		FSongClock() = default;
		explicit FSongClock(const FConfig& InConfig) : Config(InConfig) {}

		void Reset(double SongTimeMs, double WallSeconds, double Rate = 1.0);

		/** Moves the clock to WallSeconds using the rate that was active since the last update. */
		void Advance(double WallSeconds, double Rate);

		/** Corrects the clock towards the audio position (already converted to song time). */
		void Sync(double AudioSongTimeMs);

		double GetTimeMs() const { return TimeMs; }
		double GetRate() const { return Rate; }

		/** Song time at an arbitrary wall time near the last update (used to timestamp input). */
		double TimeAtWall(double WallSeconds) const;

		double GetLastDriftMs() const { return LastDriftMs; }
		int32_t GetResyncCount() const { return ResyncCount; }

	private:
		FConfig Config;
		double TimeMs = 0.0;
		/** Clock value before the last Advance; slewing never goes below it. */
		double PreviousTimeMs = 0.0;
		double LastWallSeconds = 0.0;
		double Rate = 1.0;
		double LastDriftMs = 0.0;
		int32_t ResyncCount = 0;
	};
}
