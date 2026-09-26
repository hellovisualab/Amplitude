#include "TestFramework.h"

#include "Core/AmpChart.h"
#include "Core/AmpRandom.h"
#include "Core/AmpRules.h"

#include <array>
#include <numeric>

using namespace Amp;

namespace
{
	/** Count single gems in one lane, walking across the three columns. */
	std::vector<FChartEntry> MakeLaneChart(int32_t Lane, int32_t Count, double SpacingMs, double StartMs = 1000.0, int32_t FirstId = 1)
	{
		std::vector<FChartEntry> Chart;
		for (int32_t Index = 0; Index < Count; ++Index)
		{
			FChartEntry Entry;
			Entry.Id = FirstId + Index;
			Entry.TimeMs = StartMs + SpacingMs * Index;
			Entry.Lane = Lane;
			Entry.ColumnMask = static_cast<uint8_t>(1u << static_cast<uint32_t>(Index % NumColumns));
			Chart.push_back(Entry);
		}
		return Chart;
	}

	std::vector<FChartEntry> OfLane(const std::vector<FChartEntry>& Chart, int32_t Lane)
	{
		std::vector<FChartEntry> Result;
		for (const FChartEntry& Entry : Chart)
		{
			if (Entry.Lane == Lane)
			{
				Result.push_back(Entry);
			}
		}
		return Result;
	}
}

AMP_TEST(ComboMultipliersNormal)
{
	const FDifficultyParams Params = GetDefaultDifficultyParams(EDifficulty::Normal);
	EXPECT_NEAR(GetComboMultiplier(Params, 0), 1.0, 1e-9);
	EXPECT_NEAR(GetComboMultiplier(Params, 4), 1.0, 1e-9);
	EXPECT_NEAR(GetComboMultiplier(Params, 5), 1.2, 1e-9);
	EXPECT_NEAR(GetComboMultiplier(Params, 9), 1.2, 1e-9);
	EXPECT_NEAR(GetComboMultiplier(Params, 10), 1.5, 1e-9);
	EXPECT_NEAR(GetComboMultiplier(Params, 19), 1.5, 1e-9);
	EXPECT_NEAR(GetComboMultiplier(Params, 20), 2.0, 1e-9);
	EXPECT_NEAR(GetComboMultiplier(Params, 49), 2.0, 1e-9);
	EXPECT_NEAR(GetComboMultiplier(Params, 50), 3.0, 1e-9);
	EXPECT_NEAR(GetComboMultiplier(Params, 500), 3.0, 1e-9);
}

AMP_TEST(ComboMultipliersPerDifficulty)
{
	const FDifficultyParams Mellow = GetDefaultDifficultyParams(EDifficulty::Mellow);
	EXPECT_NEAR(GetComboMultiplier(Mellow, 2), 1.0, 1e-9);
	EXPECT_NEAR(GetComboMultiplier(Mellow, 3), 1.2, 1e-9);
	EXPECT_NEAR(GetComboMultiplier(Mellow, 5), 1.5, 1e-9);
	EXPECT_NEAR(GetComboMultiplier(Mellow, 10), 2.0, 1e-9);
	EXPECT_NEAR(GetComboMultiplier(Mellow, 10000), 2.0, 1e-9); // 3.0x is never reached on Mellow

	const FDifficultyParams Brutal = GetDefaultDifficultyParams(EDifficulty::Brutal);
	EXPECT_NEAR(GetComboMultiplier(Brutal, 7), 1.0, 1e-9);
	EXPECT_NEAR(GetComboMultiplier(Brutal, 8), 1.2, 1e-9);
	EXPECT_NEAR(GetComboMultiplier(Brutal, 15), 1.5, 1e-9);
	EXPECT_NEAR(GetComboMultiplier(Brutal, 30), 2.0, 1e-9);
	EXPECT_NEAR(GetComboMultiplier(Brutal, 60), 3.0, 1e-9);

	const FDifficultyParams Insane = GetDefaultDifficultyParams(EDifficulty::Insane);
	EXPECT_NEAR(GetComboMultiplier(Insane, 11), 1.0, 1e-9);
	EXPECT_NEAR(GetComboMultiplier(Insane, 12), 1.2, 1e-9);
	EXPECT_NEAR(GetComboMultiplier(Insane, 20), 1.5, 1e-9);
	EXPECT_NEAR(GetComboMultiplier(Insane, 50), 2.0, 1e-9);
	EXPECT_NEAR(GetComboMultiplier(Insane, 99), 2.0, 1e-9);
	EXPECT_NEAR(GetComboMultiplier(Insane, 100), 3.0, 1e-9);
}

AMP_TEST(TimingWindowsPerDifficulty)
{
	const std::array<std::pair<double, double>, NumDifficulties> Expected{{{150.0, 400.0}, {100.0, 300.0}, {75.0, 250.0}, {50.0, 200.0}}};
	for (int32_t Index = 0; Index < NumDifficulties; ++Index)
	{
		const FDifficultyParams Params = GetDefaultDifficultyParams(static_cast<EDifficulty>(Index));
		const double Perfect = Expected[static_cast<size_t>(Index)].first;
		const double Good = Expected[static_cast<size_t>(Index)].second;
		EXPECT_NEAR(Params.PerfectWindowMs, Perfect, 1e-9);
		EXPECT_NEAR(Params.GoodWindowMs, Good, 1e-9);
		EXPECT_TRUE(JudgeTimingError(Params, 0.0) == EJudgement::Perfect);
		EXPECT_TRUE(JudgeTimingError(Params, Perfect) == EJudgement::Perfect);
		EXPECT_TRUE(JudgeTimingError(Params, Perfect + 0.5) == EJudgement::Good);
		EXPECT_TRUE(JudgeTimingError(Params, Good) == EJudgement::Good);
		EXPECT_TRUE(JudgeTimingError(Params, Good + 0.5) == EJudgement::None);
		EXPECT_TRUE(Params.EarlyMissWindowMs > Params.GoodWindowMs);
	}
}

AMP_TEST(DifficultyEnergyDensityAndSpeed)
{
	const std::array<int32_t, NumDifficulties> MissEnergy{-1, -3, -4, -4};
	const std::array<double, NumDifficulties> Density{0.4, 1.0, 1.5, 2.0};
	const std::array<double, NumDifficulties> Interval{10000.0, 10000.0, 8000.0, 6000.0};
	double PreviousApproach = 1e9;
	for (int32_t Index = 0; Index < NumDifficulties; ++Index)
	{
		const FDifficultyParams Params = GetDefaultDifficultyParams(static_cast<EDifficulty>(Index));
		EXPECT_EQ(Params.EnergyOnPerfect, 2);
		EXPECT_EQ(Params.EnergyOnGood, 1);
		EXPECT_EQ(Params.EnergyOnMiss, MissEnergy[static_cast<size_t>(Index)]);
		EXPECT_NEAR(Params.NoteDensity, Density[static_cast<size_t>(Index)], 1e-9);
		EXPECT_NEAR(Params.PowerupIntervalMs, Interval[static_cast<size_t>(Index)], 1e-9);
		// Higher difficulties descend faster.
		EXPECT_TRUE(Params.ApproachTimeMs < PreviousApproach);
		PreviousApproach = Params.ApproachTimeMs;
	}
	EXPECT_NEAR(GetDefaultDifficultyParams(EDifficulty::Normal).ApproachTimeMs, 2000.0, 1e-6);
	// Normal: 8-12 seconds between powerups.
	EXPECT_NEAR(GetDefaultDifficultyParams(EDifficulty::Normal).PowerupIntervalJitterMs, 2000.0, 1e-6);
}

AMP_TEST(PowerupWeightsMatchSpec)
{
	auto Share = [](EDifficulty Difficulty, EPowerupType Type)
	{
		const FDifficultyParams Params = GetDefaultDifficultyParams(Difficulty);
		const double Total = std::accumulate(Params.PowerupWeights.begin(), Params.PowerupWeights.end(), 0.0);
		return Params.PowerupWeights[static_cast<size_t>(Type)] / Total;
	};
	EXPECT_NEAR(Share(EDifficulty::Mellow, EPowerupType::Fever), 1.0 / 6.0, 1e-6);
	EXPECT_NEAR(Share(EDifficulty::Normal, EPowerupType::Score2x), 0.25, 1e-6);
	EXPECT_NEAR(Share(EDifficulty::Normal, EPowerupType::Shield), 0.15, 1e-6);
	EXPECT_NEAR(Share(EDifficulty::Brutal, EPowerupType::Fever), 0.10, 1e-6);
	EXPECT_NEAR(Share(EDifficulty::Brutal, EPowerupType::LaneCleaner), 0.20, 1e-6);
	EXPECT_NEAR(Share(EDifficulty::Brutal, EPowerupType::Score2x), 0.20, 1e-6);
	EXPECT_NEAR(Share(EDifficulty::Insane, EPowerupType::Shield), 0.08, 1e-6);
	EXPECT_NEAR(Share(EDifficulty::Insane, EPowerupType::AutoCapture), 0.08, 1e-6);
	EXPECT_NEAR(Share(EDifficulty::Insane, EPowerupType::Score2x), 0.25, 1e-6);
	EXPECT_NEAR(Share(EDifficulty::Insane, EPowerupType::Fever), 0.15, 1e-6);
}

AMP_TEST(WeightedRandomFollowsWeights)
{
	const FDifficultyParams Params = GetDefaultDifficultyParams(EDifficulty::Normal);
	FRandom Random(1234);
	std::array<int32_t, NumPowerupTypes> Counts{};
	constexpr int32_t Samples = 200000;
	for (int32_t Index = 0; Index < Samples; ++Index)
	{
		++Counts[Random.PickWeighted(Params.PowerupWeights.data(), Params.PowerupWeights.size())];
	}
	EXPECT_NEAR(Counts[0] / static_cast<double>(Samples), 0.25, 0.01);
	for (size_t Type = 1; Type < Counts.size(); ++Type)
	{
		EXPECT_NEAR(Counts[Type] / static_cast<double>(Samples), 0.15, 0.01);
	}

	FRandom A(42);
	FRandom B(42);
	for (int32_t Index = 0; Index < 100; ++Index)
	{
		EXPECT_EQ(A.NextU32(), B.NextU32());
	}
}

AMP_TEST(ScoreFormula)
{
	EXPECT_EQ(ComputeHitScore(10, 1.0, 1.0), 10);
	EXPECT_EQ(ComputeHitScore(5, 1.0, 1.0), 5);
	EXPECT_EQ(ComputeHitScore(10, 1.2, 1.0), 12);
	EXPECT_EQ(ComputeHitScore(5, 1.2, 1.0), 6);
	EXPECT_EQ(ComputeHitScore(10, 1.5, 2.0), 30);
	EXPECT_EQ(ComputeHitScore(10, 3.0, 4.0 * 3.0), 360);
	EXPECT_NEAR(ComputeAccuracyPercent(10, 0, 0), 100.0, 1e-9);
	EXPECT_NEAR(ComputeAccuracyPercent(1, 2, 1), 50.0, 1e-9);
	EXPECT_NEAR(ComputeAccuracyPercent(0, 0, 0), 0.0, 1e-9);
}

AMP_TEST(KeyLookups)
{
	EXPECT_EQ(FindLaneByInstrumentKey("drums"), 0);
	EXPECT_EQ(FindLaneByInstrumentKey("fx"), 5);
	EXPECT_EQ(FindLaneByInstrumentKey("master"), -1);
	EXPECT_EQ(FindDifficultyByKey("insane"), 3);
	EXPECT_EQ(FindPowerupByKey("auto_capture"), 5);
	EXPECT_EQ(FindNoteTypeByKey("triple"), 2);
	EXPECT_EQ(std::string(GetLaneInstrumentName(3)), std::string("Vocals"));
}

AMP_TEST(ColumnHelpers)
{
	EXPECT_EQ(CountColumns(0b000), 0);
	EXPECT_EQ(CountColumns(0b101), 2);
	EXPECT_EQ(CountColumns(0b111), 3);
	EXPECT_EQ(FirstColumn(0b100), 2);
	EXPECT_EQ(FirstColumn(0b110), 1);
	EXPECT_TRUE(NoteTypeForColumnCount(1) == ENoteType::Single);
	EXPECT_TRUE(NoteTypeForColumnCount(2) == ENoteType::Double);
	EXPECT_TRUE(NoteTypeForColumnCount(3) == ENoteType::Triple);
}

AMP_TEST(NormalizeMergesChordsWithinALane)
{
	std::vector<FChartEntry> Chart;
	Chart.push_back({1, 2000.0, 3, 0b001, ENoteType::Single});
	Chart.push_back({2, 1000.0, 1, 0b010, ENoteType::Single});
	Chart.push_back({3, 2000.0, 3, 0b100, ENoteType::Single}); // same lane and time: merged into a double
	Chart.push_back({4, 2000.0, 4, 0b010, ENoteType::Single}); // same time, other lane: separate
	Chart.push_back({5, 3000.0, 2, 0, ENoteType::Single});     // no column: dropped
	Chart.push_back({6, 3000.0, 6, 0b010, ENoteType::Single}); // no such lane: dropped
	Chart.push_back({7, 4000.0, 0, 0b11111010, ENoteType::Single}); // bits past the third column are ignored
	const std::vector<FChartEntry> Result = NormalizeChart(Chart);
	EXPECT_EQ(Result.size(), size_t(4));
	EXPECT_NEAR(Result[0].TimeMs, 1000.0, 1e-9);
	EXPECT_EQ(Result[1].Lane, 3);
	EXPECT_EQ(static_cast<int>(Result[1].ColumnMask), 0b101);
	EXPECT_TRUE(Result[1].Type == ENoteType::Double);
	EXPECT_EQ(Result[2].Lane, 4);
	EXPECT_EQ(static_cast<int>(Result[3].ColumnMask), 0b010);
}

AMP_TEST(DensityDecimationPerLane)
{
	std::vector<FChartEntry> Chart = MakeLaneChart(0, 100, 500.0);
	const std::vector<FChartEntry> Other = MakeLaneChart(4, 50, 500.0, 1250.0, 1000);
	Chart.insert(Chart.end(), Other.begin(), Other.end());
	const std::vector<FChartEntry> Mellow = NormalizeChart(ScaleChartDensity(NormalizeChart(Chart), 0.4, 150.0, 1200.0));
	const std::vector<FChartEntry> Lane0 = OfLane(Mellow, 0);
	EXPECT_EQ(Lane0.size(), size_t(40));
	EXPECT_EQ(OfLane(Mellow, 4).size(), size_t(20));
	// Kept notes are evenly spread through each lane: no gap larger than 3 source steps.
	for (size_t Index = 1; Index < Lane0.size(); ++Index)
	{
		EXPECT_TRUE(Lane0[Index].TimeMs - Lane0[Index - 1].TimeMs <= 1500.0 + 1e-6);
	}
	EXPECT_EQ(ScaleChartDensity(Chart, 1.0, 150.0, 1200.0).size(), size_t(150));
}

AMP_TEST(DensityInsertionPerLane)
{
	const std::vector<FChartEntry> Chart = MakeLaneChart(2, 100, 500.0);
	EXPECT_EQ(ScaleChartDensity(Chart, 1.5, 180.0, 1200.0).size(), size_t(150));
	// Each 500ms gap fits one extra gem at 180ms spacing, so 99 gaps cap the result at 199.
	const std::vector<FChartEntry> Insane = ScaleChartDensity(Chart, 2.0, 180.0, 1200.0);
	EXPECT_EQ(Insane.size(), size_t(199));
	for (size_t Index = 1; Index < Insane.size(); ++Index)
	{
		EXPECT_TRUE(Insane[Index].TimeMs - Insane[Index - 1].TimeMs >= 180.0 - 1e-6);
		EXPECT_EQ(CountColumns(Insane[Index].ColumnMask), 1);
		EXPECT_EQ(Insane[Index].Lane, 2);
	}
	// The gem inserted after a column-0 gem walks on to column 1.
	EXPECT_EQ(static_cast<int>(Insane[1].ColumnMask), 0b010);

	// Gaps narrower than two minimum spacings cannot take extra notes.
	EXPECT_EQ(ScaleChartDensity(MakeLaneChart(2, 50, 200.0), 2.0, 180.0, 1200.0).size(), size_t(50));

	// Wide gaps take several notes, unless they are silent stretches wider than the insert limit.
	const std::vector<FChartEntry> Sparse = MakeLaneChart(2, 10, 1000.0);
	EXPECT_EQ(ScaleChartDensity(Sparse, 2.0, 180.0, 1200.0).size(), size_t(20));
	EXPECT_EQ(ScaleChartDensity(Sparse, 2.0, 180.0, 900.0).size(), size_t(10));

	// Lanes are densified independently: a gem in another lane does not split a gap.
	std::vector<FChartEntry> TwoLanes = MakeLaneChart(0, 10, 1000.0);
	const std::vector<FChartEntry> Between = MakeLaneChart(5, 10, 1000.0, 1500.0, 100);
	TwoLanes.insert(TwoLanes.end(), Between.begin(), Between.end());
	const std::vector<FChartEntry> Doubled = ScaleChartDensity(NormalizeChart(TwoLanes), 2.0, 180.0, 1200.0);
	EXPECT_EQ(OfLane(Doubled, 0).size(), size_t(20));
	EXPECT_EQ(OfLane(Doubled, 5).size(), size_t(20));
	for (size_t Index = 1; Index < Doubled.size(); ++Index)
	{
		EXPECT_TRUE(Doubled[Index].TimeMs >= Doubled[Index - 1].TimeMs);
	}
}

AMP_TEST(ExtraChordsAndExpansion)
{
	const std::vector<FChartEntry> Chart = MakeLaneChart(1, 80, 400.0);
	const std::vector<FChartEntry> Chords = AddExtraChords(Chart, 0.125);
	int32_t Doubles = 0;
	for (const FChartEntry& Entry : Chords)
	{
		if (CountColumns(Entry.ColumnMask) == 2)
		{
			++Doubles;
			EXPECT_TRUE(Entry.Type == ENoteType::Double);
			// Chords use neighbouring buttons.
			EXPECT_TRUE(Entry.ColumnMask == 0b011 || Entry.ColumnMask == 0b110);
		}
	}
	EXPECT_EQ(Doubles, 10);

	std::vector<FChartEntry> Triple;
	Triple.push_back({7, 500.0, 2, 0b111, ENoteType::Triple});
	Triple.push_back({8, 250.0, 5, 0b001, ENoteType::Single});
	const std::vector<FNote> Notes = ExpandChart(NormalizeChart(Triple));
	EXPECT_EQ(Notes.size(), size_t(4));
	EXPECT_EQ(Notes[0].Lane, 5);
	EXPECT_EQ(Notes[0].Column, 0);
	for (size_t Index = 1; Index < 4; ++Index)
	{
		EXPECT_EQ(Notes[Index].Lane, 2);
		EXPECT_EQ(Notes[Index].Column, static_cast<int32_t>(Index) - 1);
		EXPECT_EQ(Notes[Index].ChordId, Notes[1].ChordId);
		EXPECT_EQ(Notes[Index].Id, 7);
		EXPECT_TRUE(Notes[Index].Type == ENoteType::Triple);
	}
}

AMP_TEST(StreamExpansion)
{
	std::vector<FChartEntry> Chart;
	AppendStream(Chart, 3, 1000.0, 2, 0b100, 4, 125.0);
	EXPECT_EQ(Chart.size(), size_t(4));
	EXPECT_NEAR(Chart[3].TimeMs, 1375.0, 1e-9);
	EXPECT_EQ(Chart[3].Lane, 2);
	EXPECT_TRUE(Chart[2].Type == ENoteType::Stream);

	FChartBuildOptions Options;
	Options.DensityScale = 1.0;
	const std::vector<FNote> Notes = BuildNotes(Chart, Options);
	EXPECT_EQ(Notes.size(), size_t(4));
	EXPECT_EQ(Notes[0].Column, 2);
	EXPECT_TRUE(Notes[0].Type == ENoteType::Stream);
}
