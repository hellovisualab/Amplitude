#include "TestFramework.h"

#include "Core/AmpChart.h"
#include "Core/AmpRules.h"
#include "Core/AmpSimulation.h"

#include <algorithm>

using namespace Amp;

namespace
{
	FNote MakeNote(int32_t Lane, double TimeMs)
	{
		FNote Note;
		Note.Lane = Lane;
		Note.TimeMs = TimeMs;
		return Note;
	}

	std::vector<FNote> LaneNotes(int32_t Lane, double StartMs, double SpacingMs, int32_t Count)
	{
		std::vector<FNote> Notes;
		for (int32_t Index = 0; Index < Count; ++Index)
		{
			Notes.push_back(MakeNote(Lane, StartMs + SpacingMs * Index));
		}
		return Notes;
	}

	/** A simulation with powerup spawning disabled so tests are fully scripted. */
	struct FHarness
	{
		FSimulation Sim;
		std::vector<FEvent> Events;
		double NowMs = 0.0;

		explicit FHarness(std::vector<FNote> Notes, EDifficulty Difficulty = EDifficulty::Normal, double LengthMs = 0.0, const FGameRules& Rules = FGameRules())
		{
			Sim.SetPowerupSpawningEnabled(false);
			Sim.Start(std::move(Notes), GetDefaultDifficultyParams(Difficulty), Rules, LengthMs, 7);
		}

		void AdvanceTo(double TimeMs, double StepMs = 10.0)
		{
			while (NowMs + StepMs <= TimeMs)
			{
				NowMs += StepMs;
				Sim.Advance(NowMs, StepMs);
			}
			if (NowMs < TimeMs)
			{
				const double Rest = TimeMs - NowMs;
				NowMs = TimeMs;
				Sim.Advance(NowMs, Rest);
			}
			Sim.DrainEvents(Events);
		}

		void Press(int32_t Lane, double TimeMs)
		{
			AdvanceTo(TimeMs);
			Sim.PressLane(Lane, TimeMs);
			Sim.DrainEvents(Events);
		}

		int32_t CountEvents(EEventType Type) const
		{
			return static_cast<int32_t>(std::count_if(Events.begin(), Events.end(), [Type](const FEvent& Event) { return Event.Type == Type; }));
		}

		const FEvent* LastEvent(EEventType Type) const
		{
			for (auto It = Events.rbegin(); It != Events.rend(); ++It)
			{
				if (It->Type == Type)
				{
					return &*It;
				}
			}
			return nullptr;
		}
	};
}

AMP_TEST(PerfectAndGoodHitsScoreAndEnergy)
{
	std::vector<FNote> Notes = {MakeNote(0, 1000.0), MakeNote(1, 2000.0)};
	FHarness H(Notes);
	EXPECT_EQ(H.Sim.GetEnergy(), 50);

	H.Press(0, 1040.0);
	const FEvent* Hit = H.LastEvent(EEventType::NoteHit);
	EXPECT_TRUE(Hit != nullptr && Hit->Judgement == EJudgement::Perfect);
	EXPECT_EQ(H.Sim.GetScore(), 10);
	EXPECT_EQ(H.Sim.GetEnergy(), 52);
	EXPECT_EQ(H.Sim.GetShipLane(), 0);

	H.Press(1, 1800.0); // 200ms early: Good
	Hit = H.LastEvent(EEventType::NoteHit);
	EXPECT_TRUE(Hit != nullptr && Hit->Judgement == EJudgement::Good);
	EXPECT_NEAR(Hit->OffsetMs, -200.0, 1e-9);
	EXPECT_EQ(H.Sim.GetScore(), 15);
	EXPECT_EQ(H.Sim.GetEnergy(), 53);
}

AMP_TEST(WindowBoundaries)
{
	for (const double Offset : {-100.0, 100.0})
	{
		FHarness H({MakeNote(2, 1000.0)});
		H.Press(2, 1000.0 + Offset);
		EXPECT_TRUE(H.Sim.GetNotes()[0].Judgement == EJudgement::Perfect);
	}
	for (const double Offset : {-300.0, 299.0, 150.0})
	{
		FHarness H({MakeNote(2, 1000.0)});
		H.Press(2, 1000.0 + Offset);
		EXPECT_TRUE(H.Sim.GetNotes()[0].Judgement == EJudgement::Good);
	}
}

AMP_TEST(AutoMissWhenNotePasses)
{
	FHarness H({MakeNote(3, 1000.0)});
	H.AdvanceTo(1300.0);
	EXPECT_TRUE(H.Sim.GetNotes()[0].IsPending());
	H.AdvanceTo(1310.0);
	EXPECT_TRUE(H.Sim.GetNotes()[0].Judgement == EJudgement::Miss);
	EXPECT_EQ(H.Sim.GetEnergy(), 47);
	EXPECT_EQ(H.Sim.GetLane(3).Combo, 0);
	EXPECT_EQ(H.CountEvents(EEventType::NoteMissed), 1);
}

AMP_TEST(EarlyPressMissesAndGhostPressIsFree)
{
	FHarness H({MakeNote(1, 1000.0)});
	H.Press(1, 500.0); // 500ms early: outside the early-miss window (400ms), ghost press
	EXPECT_EQ(H.CountEvents(EEventType::GhostPress), 1);
	EXPECT_EQ(H.Sim.GetEnergy(), 50);
	EXPECT_TRUE(H.Sim.GetNotes()[0].IsPending());

	H.Press(1, 650.0); // 350ms early: inside the early-miss window, costs the note
	EXPECT_TRUE(H.Sim.GetNotes()[0].Judgement == EJudgement::Miss);
	EXPECT_EQ(H.Sim.GetEnergy(), 47);

	H.Press(4, 700.0); // empty lane
	EXPECT_EQ(H.CountEvents(EEventType::GhostPress), 2);
}

AMP_TEST(ClosestNoteIsJudged)
{
	FHarness H({MakeNote(0, 1000.0), MakeNote(0, 1200.0)});
	H.Press(0, 1190.0);
	EXPECT_TRUE(H.Sim.GetNotes()[1].Judgement == EJudgement::Perfect);
	EXPECT_TRUE(H.Sim.GetNotes()[0].IsPending());
	H.AdvanceTo(1400.0);
	EXPECT_TRUE(H.Sim.GetNotes()[0].Judgement == EJudgement::Miss);
}

AMP_TEST(EnergyIsCappedAndGameOverAtZero)
{
	// 4 hits (+8) and a capture (-5) take energy to 53, then 36 auto-played Perfects (+72) hit the cap.
	FHarness Up(LaneNotes(0, 1000.0, 400.0, 40));
	for (int32_t Index = 0; Index < 40; ++Index)
	{
		Up.Press(0, 1000.0 + 400.0 * Index);
	}
	EXPECT_EQ(Up.Sim.GetEnergy(), 100);

	std::vector<FNote> Many;
	for (int32_t Index = 0; Index < 30; ++Index)
	{
		Many.push_back(MakeNote(Index % NumLanes, 1000.0 + 200.0 * Index));
	}
	FHarness Down(Many);
	Down.AdvanceTo(10000.0);
	// 50 energy / 3 per miss => the 17th miss ends the run.
	EXPECT_TRUE(Down.Sim.IsGameOver());
	EXPECT_EQ(Down.Sim.GetEnergy(), 0);
	EXPECT_EQ(Down.Sim.GetStats().Miss, 17);
	EXPECT_EQ(Down.CountEvents(EEventType::GameOver), 1);

	// Nothing changes after game over.
	const int64_t Score = Down.Sim.GetScore();
	Down.Sim.PressLane(0, 10000.0);
	EXPECT_EQ(Down.Sim.GetScore(), Score);
}

AMP_TEST(PerLaneCombosAndMultiplier)
{
	std::vector<FNote> Notes = LaneNotes(0, 1000.0, 500.0, 3);
	std::vector<FNote> Other = LaneNotes(1, 1250.0, 500.0, 3);
	Notes.insert(Notes.end(), Other.begin(), Other.end());
	FHarness H(Notes);
	H.Press(0, 1000.0);
	H.Press(1, 1250.0);
	H.Press(0, 1500.0);
	EXPECT_EQ(H.Sim.GetLane(0).Combo, 2);
	EXPECT_EQ(H.Sim.GetLane(1).Combo, 1);
	H.AdvanceTo(2100.0); // lane 1 note at 1750 is missed (deadline 2050)
	EXPECT_EQ(H.Sim.GetLane(1).Combo, 0);
	EXPECT_EQ(H.Sim.GetLane(0).Combo, 2);
}

AMP_TEST(ComboMultiplierAppliesAtThreshold)
{
	// On Mellow 1.2x starts at a combo of 3, below the capture threshold of 4, so the tier shows up before any auto-play.
	FHarness H(LaneNotes(0, 1000.0, 500.0, 3), EDifficulty::Mellow);
	H.Press(0, 1000.0);
	H.Press(0, 1500.0);
	EXPECT_EQ(H.Sim.GetScore(), 20);
	H.Press(0, 2000.0);
	EXPECT_EQ(H.Sim.GetScore(), 32); // third hit: 10 x 1.2
}

AMP_TEST(LaneCaptureAfterFourHits)
{
	FHarness H(LaneNotes(2, 1000.0, 500.0, 12));
	for (int32_t Index = 0; Index < 3; ++Index)
	{
		H.Press(2, 1000.0 + 500.0 * Index);
	}
	EXPECT_FALSE(H.Sim.GetLane(2).bCaptured);
	EXPECT_EQ(H.Sim.GetEnergy(), 56);

	H.Press(2, 2500.0);
	EXPECT_TRUE(H.Sim.GetLane(2).bCaptured);
	EXPECT_EQ(H.CountEvents(EEventType::LaneCaptured), 1);
	EXPECT_EQ(H.Sim.GetEnergy(), 56 + 2 - 5);
	EXPECT_NEAR(H.Sim.GetCaptureRemainingMs(2), 30000.0, 1e-6);

	// The remaining notes play themselves as Perfect hits and keep the combo going.
	H.Sim.MoveShip(5);
	H.AdvanceTo(7000.0);
	EXPECT_EQ(H.Sim.GetStats().AutoHits, 8);
	EXPECT_EQ(H.Sim.GetLane(2).Combo, 12);
	EXPECT_EQ(H.Sim.GetEnergy(), 53 + 16);
	EXPECT_EQ(H.Sim.GetStats().Miss, 0);
}

AMP_TEST(CaptureExpiresAndNeedsFreshStreak)
{
	std::vector<FNote> Notes = LaneNotes(4, 1000.0, 500.0, 4);
	Notes.push_back(MakeNote(4, 32000.0)); // inside the capture: auto-played
	Notes.push_back(MakeNote(4, 33000.0)); // after it expires (2500 + 30000): the player must hit it
	FHarness H(Notes);
	for (int32_t Index = 0; Index < 4; ++Index)
	{
		H.Press(4, 1000.0 + 500.0 * Index);
	}
	EXPECT_TRUE(H.Sim.GetLane(4).bCaptured);
	H.AdvanceTo(32600.0);
	EXPECT_FALSE(H.Sim.GetLane(4).bCaptured);
	EXPECT_EQ(H.CountEvents(EEventType::CaptureExpired), 1);
	EXPECT_TRUE(H.Sim.GetNotes()[4].bAutoPlayed);
	EXPECT_TRUE(H.Sim.GetNotes()[5].IsPending());
	EXPECT_EQ(H.Sim.GetLane(4).CaptureStreak, 0);
	H.AdvanceTo(33400.0);
	EXPECT_TRUE(H.Sim.GetNotes()[5].Judgement == EJudgement::Miss);
}

AMP_TEST(MissBreaksCaptureStreak)
{
	std::vector<FNote> Notes = LaneNotes(0, 1000.0, 500.0, 8);
	FHarness H(Notes);
	H.Press(0, 1000.0);
	H.Press(0, 1500.0);
	H.Press(0, 2000.0);
	H.AdvanceTo(2900.0); // miss the 4th note
	EXPECT_EQ(H.Sim.GetLane(0).CaptureStreak, 0);
	H.Press(0, 3000.0);
	H.Press(0, 3500.0);
	H.Press(0, 4000.0);
	EXPECT_FALSE(H.Sim.GetLane(0).bCaptured);
	H.Press(0, 4500.0);
	EXPECT_TRUE(H.Sim.GetLane(0).bCaptured);
}

AMP_TEST(CaptureCostsEnergyAtLowEnergy)
{
	// Burn energy down to 5 with misses in other lanes, then build a streak.
	std::vector<FNote> Notes;
	for (int32_t Index = 0; Index < 15; ++Index)
	{
		Notes.push_back(MakeNote(1 + Index % 5, 1000.0 + 100.0 * Index));
	}
	std::vector<FNote> Streak = LaneNotes(0, 5000.0, 500.0, 4);
	Notes.insert(Notes.end(), Streak.begin(), Streak.end());
	FHarness H(Notes);
	H.AdvanceTo(4000.0);
	EXPECT_EQ(H.Sim.GetEnergy(), 5); // 50 - 15 * 3
	for (int32_t Index = 0; Index < 4; ++Index)
	{
		H.Press(0, 5000.0 + 500.0 * Index);
	}
	// 5 + 4 * 2 = 13 > 5: the capture is affordable and its cost is paid.
	EXPECT_TRUE(H.Sim.GetLane(0).bCaptured);
	EXPECT_EQ(H.Sim.GetEnergy(), 8);
}

AMP_TEST(CaptureDeniedWhenUnaffordable)
{
	// With a 60 energy cost, a capture needs more than 60 energy; the streak keeps counting until then.
	FGameRules Rules;
	Rules.CaptureEnergyCost = 60;
	FHarness H(LaneNotes(0, 1000.0, 500.0, 8), EDifficulty::Normal, 0.0, Rules);
	for (int32_t Index = 0; Index < 5; ++Index)
	{
		H.Press(0, 1000.0 + 500.0 * Index);
	}
	EXPECT_FALSE(H.Sim.GetLane(0).bCaptured);
	EXPECT_EQ(H.Sim.GetLane(0).CaptureStreak, 5);
	EXPECT_EQ(H.Sim.GetEnergy(), 60);
	H.Press(0, 3500.0);
	EXPECT_TRUE(H.Sim.GetLane(0).bCaptured);
	EXPECT_EQ(H.Sim.GetEnergy(), 2);

	// Auto-Capture obeys the same rule and fizzles at low energy.
	H.Sim.ApplyPowerup(EPowerupType::AutoCapture);
	H.Sim.DrainEvents(H.Events);
	EXPECT_EQ(H.CountEvents(EEventType::AutoCaptureFailed), 1);
	EXPECT_EQ(H.Sim.GetEnergy(), 2);
	EXPECT_FALSE(H.Sim.IsGameOver());
}

AMP_TEST(MuteAfterFourMissesAndRecover)
{
	FHarness H(LaneNotes(5, 1000.0, 500.0, 6));
	H.AdvanceTo(2400.0); // 3 misses
	EXPECT_FALSE(H.Sim.GetLane(5).bMuted);
	H.AdvanceTo(2900.0); // 4th miss
	EXPECT_TRUE(H.Sim.GetLane(5).bMuted);
	EXPECT_EQ(H.Sim.GetLane(5).MuteCount, 1);
	EXPECT_EQ(H.CountEvents(EEventType::LaneMuted), 1);
	EXPECT_EQ(H.Sim.GetLane(5).ConsecutiveMisses, 4);

	H.Press(5, 3500.0);
	EXPECT_FALSE(H.Sim.GetLane(5).bMuted);
	EXPECT_EQ(H.CountEvents(EEventType::LaneUnmuted), 1);
	EXPECT_EQ(H.Sim.GetLane(5).ConsecutiveMisses, 0);
	EXPECT_TRUE(H.Sim.GetScore() > 0); // muted lanes still score
}

AMP_TEST(DoubleNotesNeedBothLanes)
{
	std::vector<FChartEntry> Chart = {{1, 1000.0, 0b001001, ENoteType::Double}};
	FHarness H(ExpandChart(Chart));
	H.Press(0, 1000.0);
	H.Press(3, 1005.0);
	EXPECT_EQ(H.Sim.GetStats().Perfect, 2);
	EXPECT_EQ(H.Sim.GetShipLane(), 3);
}

AMP_TEST(CapturedLanePressOnlyMovesShip)
{
	FHarness H(LaneNotes(0, 1000.0, 500.0, 6));
	for (int32_t Index = 0; Index < 4; ++Index)
	{
		H.Press(0, 1000.0 + 500.0 * Index);
	}
	H.Press(0, 2700.0); // captured: no ghost, no early miss
	EXPECT_EQ(H.CountEvents(EEventType::GhostPress), 0);
	EXPECT_TRUE(H.Sim.GetNotes()[4].IsPending());
	H.AdvanceTo(3000.0);
	EXPECT_TRUE(H.Sim.GetNotes()[4].bAutoPlayed);
}

AMP_TEST(Score2xStacksMultiplicatively)
{
	FHarness H({MakeNote(0, 1000.0), MakeNote(1, 1500.0), MakeNote(2, 20000.0)});
	H.Sim.ApplyPowerup(EPowerupType::Score2x);
	H.Press(0, 1000.0);
	EXPECT_EQ(H.Sim.GetScore(), 20);
	H.Sim.ApplyPowerup(EPowerupType::Score2x);
	EXPECT_NEAR(H.Sim.GetGlobalMultiplier(), 4.0, 1e-9);
	H.Press(1, 1500.0);
	EXPECT_EQ(H.Sim.GetScore(), 60);
	H.AdvanceTo(17000.0); // both 15s timers (real time) expire
	EXPECT_EQ(H.Sim.GetScore2xStacks(), 0);
	EXPECT_EQ(H.CountEvents(EEventType::PowerupEffectEnded), 2);
	H.Press(2, 20000.0);
	EXPECT_EQ(H.Sim.GetScore(), 70);
}

AMP_TEST(FeverRampsAndBreaksOnMiss)
{
	std::vector<FNote> Notes;
	for (int32_t Index = 0; Index < 20; ++Index)
	{
		Notes.push_back(MakeNote(Index % 3, 1000.0 + 300.0 * Index));
	}
	Notes.push_back(MakeNote(5, 8000.0));
	FHarness H(Notes, EDifficulty::Mellow);
	H.Sim.ApplyPowerup(EPowerupType::Fever);
	EXPECT_NEAR(H.Sim.GetGlobalMultiplier(), 1.5, 1e-9);
	H.Press(0, 1000.0);
	EXPECT_EQ(H.Sim.GetScore(), 15);
	EXPECT_NEAR(H.Sim.GetGlobalMultiplier(), 1.6, 1e-9);
	for (int32_t Index = 1; Index < 20; ++Index)
	{
		H.Press(Index % 3, 1000.0 + 300.0 * Index);
	}
	EXPECT_NEAR(H.Sim.GetGlobalMultiplier(), 3.0, 1e-9); // capped
	H.AdvanceTo(8400.0 + 100.0); // miss the lane 5 note
	EXPECT_FALSE(H.Sim.IsFeverActive());
	EXPECT_NEAR(H.Sim.GetGlobalMultiplier(), 1.0, 1e-9);
	EXPECT_EQ(H.CountEvents(EEventType::FeverBroken), 1);
}

AMP_TEST(ShieldAbsorbsOneMiss)
{
	FHarness H({MakeNote(0, 1000.0), MakeNote(1, 2000.0)});
	H.Sim.ApplyPowerup(EPowerupType::Shield);
	H.AdvanceTo(1400.0);
	EXPECT_EQ(H.Sim.GetEnergy(), 50);
	EXPECT_FALSE(H.Sim.IsShieldActive());
	EXPECT_EQ(H.CountEvents(EEventType::ShieldAbsorbedMiss), 1);
	EXPECT_EQ(H.Sim.GetLane(0).Combo, 0); // the miss still counts
	H.AdvanceTo(2400.0);
	EXPECT_EQ(H.Sim.GetEnergy(), 47);

	FHarness Expire({MakeNote(0, 40000.0)});
	Expire.Sim.ApplyPowerup(EPowerupType::Shield);
	Expire.AdvanceTo(31000.0);
	EXPECT_FALSE(Expire.Sim.IsShieldActive());
}

AMP_TEST(SlowMotionHalvesRateForTenSeconds)
{
	FHarness H({MakeNote(0, 60000.0)});
	EXPECT_NEAR(H.Sim.GetPlaybackRate(), 1.0, 1e-9);
	H.Sim.ApplyPowerup(EPowerupType::SlowMotion);
	EXPECT_NEAR(H.Sim.GetPlaybackRate(), 0.5, 1e-9);
	H.AdvanceTo(9990.0);
	EXPECT_NEAR(H.Sim.GetPlaybackRate(), 0.5, 1e-9);
	H.AdvanceTo(10010.0);
	EXPECT_NEAR(H.Sim.GetPlaybackRate(), 1.0, 1e-9);
}

AMP_TEST(LaneCleanerClearsChosenLane)
{
	std::vector<FNote> Notes = LaneNotes(3, 2000.0, 250.0, 6);
	Notes.push_back(MakeNote(1, 2100.0));
	FHarness H(Notes);
	H.AdvanceTo(1000.0);
	H.Sim.ApplyPowerup(EPowerupType::LaneCleaner);
	EXPECT_TRUE(H.Sim.IsLaneCleanerArmed());
	H.Press(3, 1000.0); // selects lane 4 (index 3) instead of firing
	EXPECT_FALSE(H.Sim.IsLaneCleanerArmed());
	const FEvent* Cleared = H.LastEvent(EEventType::LaneCleared);
	EXPECT_TRUE(Cleared != nullptr && Cleared->Lane == 3);
	// Visible notes (within the 2s approach) are cleared: 2000..3000 => 5 notes, 3250 is not yet on screen.
	EXPECT_EQ(Cleared != nullptr ? Cleared->Count : -1, 5);
	EXPECT_EQ(H.Sim.GetScore(), 0);
	EXPECT_EQ(H.Sim.GetEnergy(), 50);
	EXPECT_TRUE(H.Sim.GetNotes().back().IsPending());

	// Without a choice within 3 seconds the most crowded visible lane is cleared.
	std::vector<FNote> Later = LaneNotes(3, 5000.0, 250.0, 6);
	Later.push_back(MakeNote(1, 5100.0));
	FHarness Timeout(Later);
	Timeout.AdvanceTo(1000.0);
	Timeout.Sim.ApplyPowerup(EPowerupType::LaneCleaner);
	Timeout.AdvanceTo(3900.0);
	EXPECT_TRUE(Timeout.Sim.IsLaneCleanerArmed());
	Timeout.AdvanceTo(4100.0);
	const FEvent* Auto = Timeout.LastEvent(EEventType::LaneCleared);
	EXPECT_TRUE(Auto != nullptr && Auto->Lane == 3);
}

AMP_TEST(AutoCaptureTargetsNeediestLane)
{
	std::vector<FNote> Notes = LaneNotes(2, 1000.0, 300.0, 3);
	std::vector<FNote> Later = LaneNotes(2, 5000.0, 500.0, 5);
	std::vector<FNote> Others = LaneNotes(4, 5000.0, 500.0, 5);
	Notes.insert(Notes.end(), Later.begin(), Later.end());
	Notes.insert(Notes.end(), Others.begin(), Others.end());
	FHarness H(Notes);
	H.AdvanceTo(2000.0); // lane 3 (index 2) misses three times
	EXPECT_EQ(H.Sim.GetLane(2).ConsecutiveMisses, 3);
	const int32_t Energy = H.Sim.GetEnergy();
	H.Sim.ApplyPowerup(EPowerupType::AutoCapture);
	EXPECT_TRUE(H.Sim.GetLane(2).bCaptured);
	EXPECT_EQ(H.Sim.GetEnergy(), Energy - 5);
}

AMP_TEST(PowerupCollectionAndDespawn)
{
	FHarness H({MakeNote(0, 60000.0)});
	H.Sim.MoveShip(1);
	H.Sim.ForceSpawnPowerup(EPowerupType::Shield, 1);
	H.AdvanceTo(6000.0);
	EXPECT_EQ(H.Sim.GetPowerups().size(), size_t(1));
	// The ship sits at y = 0.9 with a 0.045 pickup band => ages 6840..7560ms of the 8s fall.
	H.AdvanceTo(7000.0);
	EXPECT_EQ(H.Sim.GetPowerups().size(), size_t(0));
	EXPECT_EQ(H.Sim.GetScore(), 500);
	EXPECT_TRUE(H.Sim.IsShieldActive());
	EXPECT_EQ(H.Sim.GetStats().PowerupsCollected, 1);

	FHarness Missed({MakeNote(0, 60000.0)});
	Missed.Sim.MoveShip(4);
	Missed.Sim.ForceSpawnPowerup(EPowerupType::Fever, 1);
	Missed.AdvanceTo(8100.0);
	EXPECT_EQ(Missed.Sim.GetPowerups().size(), size_t(0));
	EXPECT_EQ(Missed.CountEvents(EEventType::PowerupDespawned), 1);
	EXPECT_EQ(Missed.Sim.GetScore(), 0);

	// Jumping into the lane while the powerup is level with the ship picks it up immediately.
	FHarness Jump({MakeNote(0, 60000.0)});
	Jump.Sim.MoveShip(0);
	Jump.Sim.ForceSpawnPowerup(EPowerupType::SlowMotion, 5);
	Jump.AdvanceTo(7200.0);
	Jump.Press(5, 7200.0);
	EXPECT_TRUE(Jump.Sim.IsSlowMotionActive());
}

AMP_TEST(PowerupsSpawnOnSchedule)
{
	FSimulation Sim;
	Sim.Start({MakeNote(0, 600000.0)}, GetDefaultDifficultyParams(EDifficulty::Normal), FGameRules(), 0.0, 99);
	Sim.MoveShip(0);
	std::vector<FEvent> Events;
	double Previous = -1.0;
	double MinGap = 1e9;
	double MaxGap = 0.0;
	int32_t Spawned = 0;
	for (int32_t Step = 1; Step <= 12000; ++Step) // 200 seconds at 60 fps
	{
		const double Now = Step * (1000.0 / 60.0);
		Sim.Advance(Now, 1000.0 / 60.0);
		Events.clear();
		Sim.DrainEvents(Events);
		for (const FEvent& Event : Events)
		{
			if (Event.Type == EEventType::PowerupSpawned)
			{
				++Spawned;
				if (Previous >= 0.0)
				{
					MinGap = std::min(MinGap, Now - Previous);
					MaxGap = std::max(MaxGap, Now - Previous);
				}
				Previous = Now;
			}
		}
	}
	EXPECT_TRUE(Spawned >= 16 && Spawned <= 25);
	EXPECT_TRUE(MinGap >= 8000.0 - 20.0);
	EXPECT_TRUE(MaxGap <= 12000.0 + 20.0);
}

AMP_TEST(SongCompletionBonusAndSummary)
{
	std::vector<FNote> Notes = LaneNotes(0, 1000.0, 500.0, 2);
	Notes.push_back(MakeNote(3, 2000.0));
	FHarness H(Notes, EDifficulty::Normal, 5000.0);
	H.Press(0, 1000.0);
	H.Press(0, 1650.0); // Good
	H.AdvanceTo(4000.0);
	EXPECT_FALSE(H.Sim.IsComplete());
	H.AdvanceTo(5000.0);
	EXPECT_TRUE(H.Sim.IsComplete());
	EXPECT_EQ(H.CountEvents(EEventType::SongComplete), 1);
	EXPECT_EQ(H.Sim.GetScore(), 10 + 5 + 1000);

	const FRunSummary Summary = H.Sim.Summarize();
	EXPECT_TRUE(Summary.bCompleted);
	EXPECT_EQ(Summary.Perfect, 1);
	EXPECT_EQ(Summary.Good, 1);
	EXPECT_EQ(Summary.Miss, 1);
	EXPECT_NEAR(Summary.AccuracyPercent, 50.0, 1e-9);
	EXPECT_EQ(Summary.BestLane, 0);
	EXPECT_EQ(Summary.BestLaneCombo, 2);
	EXPECT_EQ(Summary.WorstLane, 3);
	EXPECT_EQ(Summary.WorstLaneMisses, 1);
}

AMP_TEST(AutoPlayAllHitsEverything)
{
	std::vector<FNote> Notes;
	for (int32_t Index = 0; Index < 30; ++Index)
	{
		Notes.push_back(MakeNote(Index % NumLanes, 500.0 + 150.0 * Index));
	}
	FHarness H(Notes);
	H.Sim.SetAutoPlayAll(true);
	H.AdvanceTo(10000.0);
	EXPECT_TRUE(H.Sim.IsComplete());
	EXPECT_EQ(H.Sim.GetStats().AutoHits, 30);
	EXPECT_EQ(H.Sim.GetStats().Miss, 0);
}

AMP_TEST(DeterministicWithSameSeed)
{
	auto Run = [](uint64_t Seed)
	{
		FSimulation Sim;
		Sim.Start({MakeNote(0, 300000.0)}, GetDefaultDifficultyParams(EDifficulty::Insane), FGameRules(), 0.0, Seed);
		std::vector<FEvent> Events;
		for (int32_t Step = 1; Step <= 6000; ++Step)
		{
			Sim.Advance(Step * 16.0, 16.0);
		}
		Sim.DrainEvents(Events);
		std::vector<int32_t> Signature;
		for (const FEvent& Event : Events)
		{
			if (Event.Type == EEventType::PowerupSpawned)
			{
				Signature.push_back(static_cast<int32_t>(Event.Powerup) * 10 + Event.Lane);
			}
		}
		return Signature;
	};
	EXPECT_TRUE(Run(5) == Run(5));
	EXPECT_FALSE(Run(5) == Run(6));
}
