#pragma once

#include "Core/AmpTypes.h"

#include <vector>

namespace Amp
{
	int32_t CountColumns(uint8_t ColumnMask);
	int32_t FirstColumn(uint8_t ColumnMask);
	ENoteType NoteTypeForColumnCount(int32_t ColumnCount);

	/** Drops invalid entries, sorts by (time, lane) and merges entries of the same lane and time into one chord. */
	std::vector<FChartEntry> NormalizeChart(std::vector<FChartEntry> Entries);

	/** Appends Count sequential entries starting at StartMs (a "stream" note in song JSON). */
	void AppendStream(std::vector<FChartEntry>& Out, int32_t Id, double StartMs, int32_t Lane, uint8_t ColumnMask, int32_t Count, double IntervalMs);

	/**
	 * Scales the number of chart entries, lane by lane so phrases keep their shape.
	 * Scale < 1 keeps an evenly spread subset (Bresenham decimation).
	 * Scale > 1 subdivides gaps between consecutive notes of a lane that are at most MaxInsertGapMs
	 * wide (so silent stretches stay silent), never closer than MinInsertGapMs to a neighbour.
	 * The result is deterministic.
	 */
	std::vector<FChartEntry> ScaleChartDensity(const std::vector<FChartEntry>& Source, double Scale, double MinInsertGapMs, double MaxInsertGapMs);

	/** Turns an evenly spread fraction of single gems into two-column chords (used for Insane). */
	std::vector<FChartEntry> AddExtraChords(const std::vector<FChartEntry>& Source, double Ratio);

	/** Expands chart entries into per-column notes sorted by (time, lane, column). */
	std::vector<FNote> ExpandChart(const std::vector<FChartEntry>& Entries);

	struct FChartBuildOptions
	{
		double DensityScale = 1.0;
		double ExtraChordRatio = 0.0;
		double MinInsertGapMs = 150.0;
		double MaxInsertGapMs = 1200.0;
	};

	/** Normalize + density scaling + chords + expansion in one call. */
	std::vector<FNote> BuildNotes(const std::vector<FChartEntry>& Chart, const FChartBuildOptions& Options);
}
