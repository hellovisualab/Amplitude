#include "Core/AmpChart.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Amp
{
	namespace
	{
		constexpr double SameTimeToleranceMs = 0.5;
		constexpr double BresenhamEpsilon = 1e-9;
		constexpr uint8_t AllColumns = static_cast<uint8_t>((1u << NumColumns) - 1u);

		/** Number of items selected at position Index when spreading Ratio items per slot. */
		int32_t BresenhamCount(size_t Index, double Ratio)
		{
			const double Before = std::floor(static_cast<double>(Index) * Ratio + BresenhamEpsilon);
			const double After = std::floor(static_cast<double>(Index + 1) * Ratio + BresenhamEpsilon);
			return static_cast<int32_t>(After - Before);
		}

		int32_t MaxId(const std::vector<FChartEntry>& Entries)
		{
			int32_t Result = 0;
			for (const FChartEntry& Entry : Entries)
			{
				Result = std::max(Result, Entry.Id);
			}
			return Result;
		}

		bool EarlierEntry(const FChartEntry& A, const FChartEntry& B)
		{
			return A.TimeMs < B.TimeMs || (A.TimeMs == B.TimeMs && A.Lane < B.Lane);
		}

		uint8_t ColumnBit(int32_t Column)
		{
			return static_cast<uint8_t>(1u << static_cast<uint32_t>(std::clamp(Column, 0, NumColumns - 1)));
		}

		/** Splits a chart into one time-ordered list per lane. */
		std::array<std::vector<FChartEntry>, NumLanes> SplitByLane(const std::vector<FChartEntry>& Source)
		{
			std::array<std::vector<FChartEntry>, NumLanes> Lanes;
			for (const FChartEntry& Entry : Source)
			{
				if (Entry.Lane >= 0 && Entry.Lane < NumLanes)
				{
					Lanes[static_cast<size_t>(Entry.Lane)].push_back(Entry);
				}
			}
			return Lanes;
		}

		std::vector<FChartEntry> DecimateLane(const std::vector<FChartEntry>& Lane, double Keep)
		{
			std::vector<FChartEntry> Result;
			for (size_t Index = 0; Index < Lane.size(); ++Index)
			{
				if (BresenhamCount(Index, Keep) > 0)
				{
					Result.push_back(Lane[Index]);
				}
			}
			return Result;
		}

		std::vector<FChartEntry> DensifyLane(const std::vector<FChartEntry>& Lane, double Scale, double MinGap, double MaxGap, int32_t& NextId)
		{
			struct FGap
			{
				size_t Index;
				int32_t Capacity;
			};
			std::vector<FGap> Gaps;
			for (size_t Index = 0; Index + 1 < Lane.size(); ++Index)
			{
				const double Width = Lane[Index + 1].TimeMs - Lane[Index].TimeMs;
				if (Width > MaxGap)
				{
					continue;
				}
				const int32_t Capacity = static_cast<int32_t>(std::floor(Width / MinGap + BresenhamEpsilon)) - 1;
				if (Capacity > 0)
				{
					Gaps.push_back({Index, Capacity});
				}
			}

			std::vector<int32_t> Inserts(Lane.size(), 0);
			if (!Gaps.empty())
			{
				const double Target = std::round(static_cast<double>(Lane.size()) * (Scale - 1.0));
				const double PerGap = Target / static_cast<double>(Gaps.size());
				for (size_t GapIndex = 0; GapIndex < Gaps.size(); ++GapIndex)
				{
					Inserts[Gaps[GapIndex].Index] = std::min(Gaps[GapIndex].Capacity, BresenhamCount(GapIndex, PerGap));
				}
			}

			std::vector<FChartEntry> Result;
			for (size_t Index = 0; Index < Lane.size(); ++Index)
			{
				const FChartEntry& Current = Lane[Index];
				Result.push_back(Current);
				const int32_t Count = Inserts[Index];
				if (Count <= 0)
				{
					continue;
				}
				const FChartEntry& Next = Lane[Index + 1];
				for (int32_t Insert = 1; Insert <= Count; ++Insert)
				{
					// Inserted gems walk across the columns so the extra notes feel like runs, not repeats.
					FChartEntry Added;
					Added.Id = NextId++;
					Added.TimeMs = Current.TimeMs + (Next.TimeMs - Current.TimeMs) * static_cast<double>(Insert) / static_cast<double>(Count + 1);
					Added.Lane = Current.Lane;
					Added.ColumnMask = ColumnBit((FirstColumn(Current.ColumnMask) + Insert) % NumColumns);
					Added.Type = ENoteType::Single;
					Result.push_back(Added);
				}
			}
			return Result;
		}
	}

	int32_t CountColumns(uint8_t ColumnMask)
	{
		int32_t Count = 0;
		for (int32_t Column = 0; Column < NumColumns; ++Column)
		{
			Count += (ColumnMask >> Column) & 1;
		}
		return Count;
	}

	int32_t FirstColumn(uint8_t ColumnMask)
	{
		for (int32_t Column = 0; Column < NumColumns; ++Column)
		{
			if ((ColumnMask >> Column) & 1)
			{
				return Column;
			}
		}
		return 1;
	}

	ENoteType NoteTypeForColumnCount(int32_t ColumnCount)
	{
		if (ColumnCount >= 3)
		{
			return ENoteType::Triple;
		}
		return ColumnCount == 2 ? ENoteType::Double : ENoteType::Single;
	}

	std::vector<FChartEntry> NormalizeChart(std::vector<FChartEntry> Entries)
	{
		std::vector<FChartEntry> Valid;
		Valid.reserve(Entries.size());
		for (FChartEntry& Entry : Entries)
		{
			Entry.ColumnMask &= AllColumns;
			if (Entry.ColumnMask != 0 && Entry.Lane >= 0 && Entry.Lane < NumLanes && std::isfinite(Entry.TimeMs))
			{
				Valid.push_back(Entry);
			}
		}

		std::stable_sort(Valid.begin(), Valid.end(), EarlierEntry);

		std::vector<FChartEntry> Result;
		Result.reserve(Valid.size());
		for (const FChartEntry& Entry : Valid)
		{
			// Look back through entries at (almost) the same time for one in the same lane.
			bool bMerged = false;
			for (auto It = Result.rbegin(); It != Result.rend() && std::abs(It->TimeMs - Entry.TimeMs) <= SameTimeToleranceMs; ++It)
			{
				if (It->Lane == Entry.Lane)
				{
					It->ColumnMask |= Entry.ColumnMask;
					if (It->Type != ENoteType::Stream)
					{
						It->Type = NoteTypeForColumnCount(CountColumns(It->ColumnMask));
					}
					bMerged = true;
					break;
				}
			}
			if (!bMerged)
			{
				Result.push_back(Entry);
			}
		}
		return Result;
	}

	void AppendStream(std::vector<FChartEntry>& Out, int32_t Id, double StartMs, int32_t Lane, uint8_t ColumnMask, int32_t Count, double IntervalMs)
	{
		const int32_t SafeCount = std::max(1, Count);
		const double SafeInterval = std::max(1.0, IntervalMs);
		for (int32_t Step = 0; Step < SafeCount; ++Step)
		{
			FChartEntry Entry;
			Entry.Id = Id;
			Entry.TimeMs = StartMs + SafeInterval * Step;
			Entry.Lane = Lane;
			Entry.ColumnMask = ColumnMask;
			Entry.Type = ENoteType::Stream;
			Out.push_back(Entry);
		}
	}

	std::vector<FChartEntry> ScaleChartDensity(const std::vector<FChartEntry>& Source, double Scale, double MinInsertGapMs, double MaxInsertGapMs)
	{
		if (Source.empty() || std::abs(Scale - 1.0) < 1e-6)
		{
			return Source;
		}

		int32_t NextId = MaxId(Source) + 1;
		std::vector<FChartEntry> Result;
		for (const std::vector<FChartEntry>& Lane : SplitByLane(Source))
		{
			std::vector<FChartEntry> Scaled = Scale < 1.0
				? DecimateLane(Lane, std::max(0.01, Scale))
				: DensifyLane(Lane, Scale, std::max(1.0, MinInsertGapMs), std::max(MinInsertGapMs, MaxInsertGapMs), NextId);
			Result.insert(Result.end(), Scaled.begin(), Scaled.end());
		}
		if (Result.empty())
		{
			Result.push_back(Source.front());
		}
		std::stable_sort(Result.begin(), Result.end(), EarlierEntry);
		return Result;
	}

	std::vector<FChartEntry> AddExtraChords(const std::vector<FChartEntry>& Source, double Ratio)
	{
		std::vector<FChartEntry> Result = Source;
		if (Ratio <= 0.0)
		{
			return Result;
		}

		size_t SingleIndex = 0;
		for (FChartEntry& Entry : Result)
		{
			if (CountColumns(Entry.ColumnMask) != 1)
			{
				continue;
			}
			if (BresenhamCount(SingleIndex++, Ratio) > 0)
			{
				const int32_t Column = FirstColumn(Entry.ColumnMask);
				const int32_t Partner = Column == NumColumns - 1 ? Column - 1 : Column + 1;
				Entry.ColumnMask = static_cast<uint8_t>(Entry.ColumnMask | ColumnBit(Partner));
				Entry.Type = ENoteType::Double;
			}
		}
		return Result;
	}

	std::vector<FNote> ExpandChart(const std::vector<FChartEntry>& Entries)
	{
		std::vector<FNote> Notes;
		Notes.reserve(Entries.size() + Entries.size() / 4);
		for (size_t EntryIndex = 0; EntryIndex < Entries.size(); ++EntryIndex)
		{
			const FChartEntry& Entry = Entries[EntryIndex];
			for (int32_t Column = 0; Column < NumColumns; ++Column)
			{
				if (((Entry.ColumnMask >> Column) & 1) == 0)
				{
					continue;
				}
				FNote Note;
				Note.Id = Entry.Id;
				Note.ChordId = static_cast<int32_t>(EntryIndex);
				Note.TimeMs = Entry.TimeMs;
				Note.Lane = Entry.Lane;
				Note.Column = Column;
				Note.Type = Entry.Type;
				Notes.push_back(Note);
			}
		}
		std::stable_sort(Notes.begin(), Notes.end(), [](const FNote& A, const FNote& B)
		{
			if (A.TimeMs != B.TimeMs)
			{
				return A.TimeMs < B.TimeMs;
			}
			return A.Lane < B.Lane || (A.Lane == B.Lane && A.Column < B.Column);
		});
		return Notes;
	}

	std::vector<FNote> BuildNotes(const std::vector<FChartEntry>& Chart, const FChartBuildOptions& Options)
	{
		std::vector<FChartEntry> Entries = NormalizeChart(Chart);
		Entries = ScaleChartDensity(Entries, Options.DensityScale, Options.MinInsertGapMs, Options.MaxInsertGapMs);
		Entries = AddExtraChords(Entries, Options.ExtraChordRatio);
		return ExpandChart(Entries);
	}
}
