#include "Core/AmpChart.h"

#include <algorithm>
#include <cmath>

namespace Amp
{
	namespace
	{
		constexpr double SameTimeToleranceMs = 0.5;
		constexpr double BresenhamEpsilon = 1e-9;

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
	}

	int32_t CountLanes(uint8_t LaneMask)
	{
		int32_t Count = 0;
		for (int32_t Lane = 0; Lane < NumLanes; ++Lane)
		{
			Count += (LaneMask >> Lane) & 1;
		}
		return Count;
	}

	int32_t FirstLane(uint8_t LaneMask)
	{
		for (int32_t Lane = 0; Lane < NumLanes; ++Lane)
		{
			if ((LaneMask >> Lane) & 1)
			{
				return Lane;
			}
		}
		return -1;
	}

	ENoteType NoteTypeForLaneCount(int32_t LaneCount)
	{
		if (LaneCount >= 3)
		{
			return ENoteType::Triple;
		}
		return LaneCount == 2 ? ENoteType::Double : ENoteType::Single;
	}

	std::vector<FChartEntry> NormalizeChart(std::vector<FChartEntry> Entries)
	{
		const uint8_t AllLanes = static_cast<uint8_t>((1u << NumLanes) - 1u);

		std::vector<FChartEntry> Valid;
		Valid.reserve(Entries.size());
		for (FChartEntry& Entry : Entries)
		{
			Entry.LaneMask &= AllLanes;
			if (Entry.LaneMask != 0 && std::isfinite(Entry.TimeMs))
			{
				Valid.push_back(Entry);
			}
		}

		std::stable_sort(Valid.begin(), Valid.end(), [](const FChartEntry& A, const FChartEntry& B) { return A.TimeMs < B.TimeMs; });

		std::vector<FChartEntry> Result;
		Result.reserve(Valid.size());
		for (const FChartEntry& Entry : Valid)
		{
			if (!Result.empty() && std::abs(Result.back().TimeMs - Entry.TimeMs) <= SameTimeToleranceMs)
			{
				FChartEntry& Merged = Result.back();
				Merged.LaneMask |= Entry.LaneMask;
				if (Merged.Type != ENoteType::Stream)
				{
					Merged.Type = NoteTypeForLaneCount(CountLanes(Merged.LaneMask));
				}
				continue;
			}
			Result.push_back(Entry);
		}
		return Result;
	}

	void AppendStream(std::vector<FChartEntry>& Out, int32_t Id, double StartMs, uint8_t LaneMask, int32_t Count, double IntervalMs)
	{
		const int32_t SafeCount = std::max(1, Count);
		const double SafeInterval = std::max(1.0, IntervalMs);
		for (int32_t Step = 0; Step < SafeCount; ++Step)
		{
			FChartEntry Entry;
			Entry.Id = Id;
			Entry.TimeMs = StartMs + SafeInterval * Step;
			Entry.LaneMask = LaneMask;
			Entry.Type = ENoteType::Stream;
			Out.push_back(Entry);
		}
	}

	std::vector<FChartEntry> ScaleChartDensity(const std::vector<FChartEntry>& Source, double Scale, double MinInsertGapMs)
	{
		if (Source.empty() || std::abs(Scale - 1.0) < 1e-6)
		{
			return Source;
		}

		if (Scale < 1.0)
		{
			const double Keep = std::max(0.01, Scale);
			std::vector<FChartEntry> Result;
			Result.reserve(static_cast<size_t>(static_cast<double>(Source.size()) * Keep) + 1);
			for (size_t Index = 0; Index < Source.size(); ++Index)
			{
				if (BresenhamCount(Index, Keep) > 0)
				{
					Result.push_back(Source[Index]);
				}
			}
			if (Result.empty())
			{
				Result.push_back(Source.front());
			}
			return Result;
		}

		// Scale > 1: insert notes into the gaps that are wide enough.
		const double MinGap = std::max(1.0, MinInsertGapMs);
		struct FGap
		{
			size_t Index;
			int32_t Capacity;
		};
		std::vector<FGap> Gaps;
		for (size_t Index = 0; Index + 1 < Source.size(); ++Index)
		{
			const double Width = Source[Index + 1].TimeMs - Source[Index].TimeMs;
			const int32_t Capacity = static_cast<int32_t>(std::floor(Width / MinGap + BresenhamEpsilon)) - 1;
			if (Capacity > 0)
			{
				Gaps.push_back({Index, Capacity});
			}
		}

		std::vector<int32_t> Inserts(Source.size(), 0);
		if (!Gaps.empty())
		{
			const double Target = std::round(static_cast<double>(Source.size()) * (Scale - 1.0));
			const double PerGap = Target / static_cast<double>(Gaps.size());
			for (size_t GapIndex = 0; GapIndex < Gaps.size(); ++GapIndex)
			{
				Inserts[Gaps[GapIndex].Index] = std::min(Gaps[GapIndex].Capacity, BresenhamCount(GapIndex, PerGap));
			}
		}

		int32_t NextId = MaxId(Source) + 1;
		std::vector<FChartEntry> Result;
		Result.reserve(static_cast<size_t>(static_cast<double>(Source.size()) * Scale) + 1);
		for (size_t Index = 0; Index < Source.size(); ++Index)
		{
			const FChartEntry& Current = Source[Index];
			Result.push_back(Current);

			const int32_t Count = Inserts[Index];
			if (Count <= 0)
			{
				continue;
			}

			const FChartEntry& Next = Source[Index + 1];
			for (int32_t Insert = 1; Insert <= Count; ++Insert)
			{
				// First half of the inserted notes echoes the current lane, the second half leads into the next one.
				const bool bEchoCurrent = Insert * 2 <= Count + 1;
				const int32_t Lane = FirstLane(bEchoCurrent ? Current.LaneMask : Next.LaneMask);

				FChartEntry Added;
				Added.Id = NextId++;
				Added.TimeMs = Current.TimeMs + (Next.TimeMs - Current.TimeMs) * static_cast<double>(Insert) / static_cast<double>(Count + 1);
				Added.LaneMask = static_cast<uint8_t>(1u << static_cast<uint32_t>(std::max(0, Lane)));
				Added.Type = ENoteType::Single;
				Result.push_back(Added);
			}
		}
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
			if (CountLanes(Entry.LaneMask) != 1)
			{
				continue;
			}
			if (BresenhamCount(SingleIndex++, Ratio) > 0)
			{
				// Pair the note with the lane on the opposite half of the field so both hands are involved.
				const int32_t Lane = FirstLane(Entry.LaneMask);
				const int32_t Partner = (Lane + NumLanes / 2) % NumLanes;
				Entry.LaneMask = static_cast<uint8_t>(Entry.LaneMask | (1u << static_cast<uint32_t>(Partner)));
				Entry.Type = ENoteType::Double;
			}
		}
		return Result;
	}

	std::vector<FNote> ExpandChart(const std::vector<FChartEntry>& Entries)
	{
		std::vector<FNote> Notes;
		Notes.reserve(Entries.size() * 2);
		for (size_t EntryIndex = 0; EntryIndex < Entries.size(); ++EntryIndex)
		{
			const FChartEntry& Entry = Entries[EntryIndex];
			for (int32_t Lane = 0; Lane < NumLanes; ++Lane)
			{
				if (((Entry.LaneMask >> Lane) & 1) == 0)
				{
					continue;
				}
				FNote Note;
				Note.Id = Entry.Id;
				Note.ChordId = static_cast<int32_t>(EntryIndex);
				Note.TimeMs = Entry.TimeMs;
				Note.Lane = Lane;
				Note.Type = Entry.Type;
				Notes.push_back(Note);
			}
		}
		std::stable_sort(Notes.begin(), Notes.end(), [](const FNote& A, const FNote& B)
		{
			return A.TimeMs < B.TimeMs || (A.TimeMs == B.TimeMs && A.Lane < B.Lane);
		});
		return Notes;
	}

	std::vector<FNote> BuildNotes(const std::vector<FChartEntry>& Chart, const FChartBuildOptions& Options)
	{
		std::vector<FChartEntry> Entries = NormalizeChart(Chart);
		Entries = ScaleChartDensity(Entries, Options.DensityScale, Options.MinInsertGapMs);
		Entries = AddExtraChords(Entries, Options.ExtraChordRatio);
		return ExpandChart(Entries);
	}
}
