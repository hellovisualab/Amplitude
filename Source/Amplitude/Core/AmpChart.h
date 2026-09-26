#pragma once

#include "Core/AmpTypes.h"

#include <vector>

namespace Amp
{
	int32_t CountLanes(uint8_t LaneMask);
	int32_t FirstLane(uint8_t LaneMask);
	ENoteType NoteTypeForLaneCount(int32_t LaneCount);

	/** Sorts entries by time and merges duplicates at the same timestamp into one multi-lane entry. */
	std::vector<FChartEntry> NormalizeChart(std::vector<FChartEntry> Entries);

	/** Appends Count sequential entries starting at StartMs (a "stream" note in song JSON). */
	void AppendStream(std::vector<FChartEntry>& Out, int32_t Id, double StartMs, uint8_t LaneMask, int32_t Count, double IntervalMs);

	/**
	 * Scales the number of chart entries.
	 * Scale < 1 keeps an evenly spread subset (Bresenham decimation).
	 * Scale > 1 subdivides gaps between consecutive entries, never placing a new note closer than
	 * MinInsertGapMs to its neighbours. The result is deterministic.
	 */
	std::vector<FChartEntry> ScaleChartDensity(const std::vector<FChartEntry>& Source, double Scale, double MinInsertGapMs);

	/** Turns an evenly spread fraction of single notes into doubles (used for Insane). */
	std::vector<FChartEntry> AddExtraChords(const std::vector<FChartEntry>& Source, double Ratio);

	/** Expands chart entries into per-lane notes sorted by (time, lane). */
	std::vector<FNote> ExpandChart(const std::vector<FChartEntry>& Entries);

	struct FChartBuildOptions
	{
		double DensityScale = 1.0;
		double ExtraChordRatio = 0.0;
		double MinInsertGapMs = 180.0;
	};

	/** Normalize + density scaling + chords + expansion in one call. */
	std::vector<FNote> BuildNotes(const std::vector<FChartEntry>& Chart, const FChartBuildOptions& Options);
}
