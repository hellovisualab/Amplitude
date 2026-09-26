#include "TestFramework.h"

#include "Core/AmpChart.h"
#include "Core/AmpRandom.h"
#include "Core/AmpRules.h"

#include <array>
#include <numeric>

using namespace Amp;

namespace
{
	std::vector<FChartEntry> MakeEvenChart(int32_t Count, double SpacingMs, int32_t LaneCycle = NumLanes)
	{
		std::vector<FChartEntry> Chart;
		for (int32_t Index = 0; Index < Count; ++Index)
		{
			FChartEntry Entry;
			Entry.Id = Index + 1;
			Entry.TimeMs = 1000.0 + SpacingMs * Index;
			Entry.LaneMask = static_cast<uint8_t>(1u << static_cast<uint32_t>(Index % LaneCycle));
			Chart.push_back(Entry);
		}
		return Chart;
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

AMP_TEST(NormalizeMergesChords)
{
	std::vector<FChartEntry> Chart;
	Chart.push_back({1, 2000.0, 0b000001, ENoteType::Single});
	Chart.push_back({2, 1000.0, 0b000010, ENoteType::Single});
	Chart.push_back({3, 2000.0, 0b001000, ENoteType::Single});
	Chart.push_back({4, 3000.0, 0, ENoteType::Single}); // no lane: dropped
	const std::vector<FChartEntry> Result = NormalizeChart(Chart);
	EXPECT_EQ(Result.size(), size_t(2));
	EXPECT_NEAR(Result[0].TimeMs, 1000.0, 1e-9);
	EXPECT_EQ(static_cast<int>(Result[1].LaneMask), 0b001001);
	EXPECT_TRUE(Result[1].Type == ENoteType::Double);
}

AMP_TEST(DensityDecimation)
{
	const std::vector<FChartEntry> Chart = MakeEvenChart(100, 500.0);
	const std::vector<FChartEntry> Mellow = ScaleChartDensity(Chart, 0.4, 180.0);
	EXPECT_EQ(Mellow.size(), size_t(40));
	// Kept notes are evenly spread: no gap larger than 3 source steps.
	for (size_t Index = 1; Index < Mellow.size(); ++Index)
	{
		EXPECT_TRUE(Mellow[Index].TimeMs - Mellow[Index - 1].TimeMs <= 1500.0 + 1e-6);
	}
	EXPECT_EQ(ScaleChartDensity(Chart, 1.0, 180.0).size(), size_t(100));
}

AMP_TEST(DensityInsertion)
{
	const std::vector<FChartEntry> Chart = MakeEvenChart(100, 500.0);
	const std::vector<FChartEntry> Brutal = ScaleChartDensity(Chart, 1.5, 180.0);
	const std::vector<FChartEntry> Insane = ScaleChartDensity(Chart, 2.0, 180.0);
	EXPECT_EQ(Brutal.size(), size_t(150));
	// 99 gaps can absorb at most 99 inserts at 1 per gap... but each 500ms gap fits 1 note at 180ms spacing.
	EXPECT_EQ(Insane.size(), size_t(199));
	for (size_t Index = 1; Index < Insane.size(); ++Index)
	{
		EXPECT_TRUE(Insane[Index].TimeMs - Insane[Index - 1].TimeMs >= 180.0 - 1e-6);
		EXPECT_EQ(CountLanes(Insane[Index].LaneMask), 1);
	}

	// Gaps narrower than two minimum spacings cannot take extra notes.
	const std::vector<FChartEntry> Dense = MakeEvenChart(50, 200.0);
	EXPECT_EQ(ScaleChartDensity(Dense, 2.0, 180.0).size(), size_t(50));

	// Wide gaps can take several notes.
	const std::vector<FChartEntry> Sparse = MakeEvenChart(10, 2000.0);
	EXPECT_EQ(ScaleChartDensity(Sparse, 2.0, 180.0).size(), size_t(20));
}

AMP_TEST(ExtraChordsAndExpansion)
{
	const std::vector<FChartEntry> Chart = MakeEvenChart(80, 400.0);
	const std::vector<FChartEntry> Chords = AddExtraChords(Chart, 0.125);
	int32_t Doubles = 0;
	for (const FChartEntry& Entry : Chords)
	{
		if (CountLanes(Entry.LaneMask) == 2)
		{
			++Doubles;
			EXPECT_TRUE(Entry.Type == ENoteType::Double);
		}
	}
	EXPECT_EQ(Doubles, 10);

	std::vector<FChartEntry> Triple;
	Triple.push_back({7, 500.0, 0b000111, ENoteType::Triple});
	Triple.push_back({8, 250.0, 0b100000, ENoteType::Single});
	const std::vector<FNote> Notes = ExpandChart(NormalizeChart(Triple));
	EXPECT_EQ(Notes.size(), size_t(4));
	EXPECT_EQ(Notes[0].Lane, 5);
	EXPECT_EQ(Notes[1].Lane, 0);
	EXPECT_EQ(Notes[3].Lane, 2);
	EXPECT_EQ(Notes[1].ChordId, Notes[3].ChordId);
	EXPECT_EQ(Notes[1].Id, 7);
}

AMP_TEST(StreamExpansion)
{
	std::vector<FChartEntry> Chart;
	AppendStream(Chart, 3, 1000.0, 0b000100, 4, 125.0);
	EXPECT_EQ(Chart.size(), size_t(4));
	EXPECT_NEAR(Chart[3].TimeMs, 1375.0, 1e-9);
	EXPECT_TRUE(Chart[2].Type == ENoteType::Stream);

	FChartBuildOptions Options;
	Options.DensityScale = 1.0;
	EXPECT_EQ(BuildNotes(Chart, Options).size(), size_t(4));
}
