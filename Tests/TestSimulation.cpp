#include "TestFramework.h"

#include "Core/AmpChart.h"
#include "Core/AmpRules.h"
#include "Core/AmpSimulation.h"

#include <algorithm>

using namespace Amp;

namespace
{
	constexpr int32_t Left = 0;
	constexpr int32_t Middle = 1;
	constexpr int32_t Right = 2;

	FNote MakeNote(int32_t Lane, double TimeMs, int32_t Column = Middle)
	{
		FNote Note;
		Note.Lane = Lane;
		Note.Column = Column;
		Note.TimeMs = TimeMs;
		return Note;
	}

	std::vector<FNote> LaneNotes(int32_t Lane, double StartMs, double SpacingMs, int32_t Count, int32_t Column = Middle)
	{
		std::vector<FNote> Notes;
		for (int32_t Index = 0; Index < Count; ++Index)
		{
			Notes.push_back(MakeNote(Lane, StartMs + SpacingMs * Index, Column));
		}
		return Notes;
	}

	/** A simulation with powerup spawning disabled so tests are fully scripted. The ship starts in lane 2. */
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

		/** Fires a gem button in the ship's current lane. */
		void Press(int32_t Column, double TimeMs)
		{
			AdvanceTo(TimeMs);
			Sim.PressColumn(Column, TimeMs);
			Sim.DrainEvents(Events);
		}

		/** Jumps to a lane and fires the middle button. */
		void PressLane(int32_t Lane, double TimeMs, int32_t Column = Middle)
		{
			AdvanceTo(TimeMs);
			Sim.PressLane(Lane, Column, TimeMs);
			Sim.DrainEvents(Events);
		}

		void Move(int32_t Lane, double TimeMs)
		{
			AdvanceTo(TimeMs);
			Sim.MoveShip(Lane, TimeMs);
			Sim.DrainEvents(Events);
		}

		void Step(int32_t Direction, double TimeMs)
		{
			AdvanceTo(TimeMs);
			Sim.StepShip(Direction, TimeMs);
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
	EXPECT_EQ(H.Sim.GetShipLane(), 2);

	H.PressLane(0, 1040.0);
	const FEvent* Hit = H.LastEvent(EEventType::NoteHit);
	EXPECT_TRUE(Hit != nullptr && Hit->Judgement == EJudgement::Perfect && Hit->Column == Middle);
	EXPECT_EQ(H.Sim.GetScore(), 10);
	EXPECT_EQ(H.Sim.GetEnergy(), 52);
	EXPECT_EQ(H.Sim.GetShipLane(), 0);

	H.PressLane(1, 1800.0); // 200ms early: Good
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
		FHarness H({MakeNote(2, 1000.0, Right)});
		H.Press(Right, 1000.0 + Offset);
		EXPECT_TRUE(H.Sim.GetNotes()[0].Judgement == EJudgement::Perfect);
	}
	for (const double Offset : {-300.0, 299.0, 150.0})
	{
		FHarness H({MakeNote(2, 1000.0, Left)});
		H.Press(Left, 1000.0 + Offset);
		EXPECT_TRUE(H.Sim.GetNotes()[0].Judgement == EJudgement::Good);
	}
}

AMP_TEST(ShipStepsBetweenLanesAndStopsAtEdges)
{
	FHarness H({MakeNote(0, 60000.0)});
	H.Step(-1, 100.0);
	H.Step(-1, 200.0);
	EXPECT_EQ(H.Sim.GetShipLane(), 0);
	H.Step(-1, 300.0); // already at the left edge
	EXPECT_EQ(H.Sim.GetShipLane(), 0);
	EXPECT_EQ(H.CountEvents(EEventType::ShipMoved), 2);
	const FEvent* Moved = H.LastEvent(EEventType::ShipMoved);
	EXPECT_TRUE(Moved != nullptr && Moved->Lane == 0 && Moved->Count == 1 && !Moved->bAuto);

	for (int32_t Step = 0; Step < 8; ++Step)
	{
		H.Step(1, 400.0 + 10.0 * Step);
	}
	EXPECT_EQ(H.Sim.GetShipLane(), NumLanes - 1);
	EXPECT_EQ(H.CountEvents(EEventType::ShipMoved), 2 + NumLanes - 1);

	// The lane history remembers where the ship was.
	EXPECT_EQ(H.Sim.GetShipLaneAt(50.0), 2);
	EXPECT_EQ(H.Sim.GetShipLaneAt(250.0), 0);
	EXPECT_EQ(H.Sim.GetShipLaneAt(415.0), 2);
}

AMP_TEST(AutoMissOnlyInTheShipLane)
{
	FHarness H({MakeNote(3, 1000.0)});
	H.Move(3, 0.0);
	H.AdvanceTo(1300.0);
	EXPECT_TRUE(H.Sim.GetNotes()[0].IsPending());
	H.AdvanceTo(1310.0);
	EXPECT_TRUE(H.Sim.GetNotes()[0].Judgement == EJudgement::Miss);
	EXPECT_EQ(H.Sim.GetEnergy(), 47);
	EXPECT_EQ(H.Sim.GetLane(3).Combo, 0);
	EXPECT_EQ(H.CountEvents(EEventType::NoteMissed), 1);
}

AMP_TEST(NotesInOtherLanesAreSkippedForFree)
{
	FHarness H(LaneNotes(4, 1000.0, 250.0, 6));
	H.AdvanceTo(3000.0);
	EXPECT_EQ(H.CountEvents(EEventType::NoteSkipped), 6);
	EXPECT_EQ(H.CountEvents(EEventType::NoteMissed), 0);
	EXPECT_EQ(H.Sim.GetEnergy(), 50);
	EXPECT_FALSE(H.Sim.GetLane(4).bMuted);
	EXPECT_EQ(H.Sim.GetStats().Skipped, 6);
	EXPECT_EQ(H.Sim.GetLane(4).Skipped, 6);
	EXPECT_TRUE(H.Sim.GetNotes()[0].Judgement == EJudgement::Skipped);

	const FRunSummary Summary = H.Sim.Summarize();
	EXPECT_EQ(Summary.Skipped, 6);
	EXPECT_EQ(Summary.Miss, 0);
}

AMP_TEST(LaneOccupiedAtNoteTimeDecidesMissOrSkip)
{
	// In the lane when the gem reaches the hit line, then leaving without firing: a miss.
	FHarness Leave({MakeNote(3, 1000.0)});
	Leave.Move(3, 500.0);
	Leave.Move(4, 1100.0);
	Leave.AdvanceTo(1400.0);
	EXPECT_TRUE(Leave.Sim.GetNotes()[0].Judgement == EJudgement::Miss);

	// Arriving just after the gem passed the line: it is skipped, but can still be hit in the window.
	FHarness Late({MakeNote(3, 1000.0), MakeNote(3, 5000.0)});
	Late.Move(3, 1100.0);
	Late.Press(Middle, 1150.0);
	EXPECT_TRUE(Late.Sim.GetNotes()[0].Judgement == EJudgement::Good);

	FHarness Pass({MakeNote(3, 1000.0)});
	Pass.Move(3, 1100.0);
	Pass.AdvanceTo(1400.0);
	EXPECT_TRUE(Pass.Sim.GetNotes()[0].Judgement == EJudgement::Skipped);
	EXPECT_EQ(Pass.Sim.GetEnergy(), 50);
}

AMP_TEST(MusicBuildsUpLaneByLane)
{
	std::vector<FNote> Notes = LaneNotes(2, 1000.0, 500.0, 8);
	std::vector<FNote> Other = LaneNotes(4, 1000.0, 500.0, 8);
	Notes.insert(Notes.end(), Other.begin(), Other.end());
	FHarness H(Notes);
	for (int32_t Lane = 0; Lane < NumLanes; ++Lane)
	{
		EXPECT_NEAR(H.Sim.GetLaneMixGain(Lane), 0.0, 1e-9); // nothing plays until it is played
	}

	H.Press(Middle, 1000.0);
	EXPECT_NEAR(H.Sim.GetLaneMixGain(2), 1.0, 1e-9); // a hit brings the instrument in
	EXPECT_NEAR(H.Sim.GetLaneMixGain(4), 0.0, 1e-9);
	H.AdvanceTo(1400.0);
	EXPECT_NEAR(H.Sim.GetLaneMixGain(2), 1.0, 1e-9); // and it keeps playing between gems

	H.AdvanceTo(1900.0); // the 1500 gem is missed
	EXPECT_NEAR(H.Sim.GetLaneMixGain(2), 0.0, 1e-9);
	EXPECT_FALSE(H.Sim.GetLane(2).bLive);

	H.Press(Middle, 2000.0);
	EXPECT_NEAR(H.Sim.GetLaneMixGain(2), 1.0, 1e-9);
	H.Move(3, 2200.0); // leaving: the next gem is skipped and the instrument drops out
	H.AdvanceTo(2900.0);
	EXPECT_NEAR(H.Sim.GetLaneMixGain(2), 0.0, 1e-9);

	// A captured lane plays by itself, then falls silent when the capture ends.
	FGameRules Rules;
	Rules.CaptureDurationMs = 3000.0;
	Rules.bAutoAdvanceOnCapture = false;
	FHarness Capture(LaneNotes(2, 1000.0, 500.0, 6), EDifficulty::Normal, 20000.0, Rules);
	for (int32_t Index = 0; Index < 4; ++Index)
	{
		Capture.Press(Middle, 1000.0 + 500.0 * Index);
	}
	EXPECT_TRUE(Capture.Sim.GetLane(2).bCaptured);
	Capture.Move(0, 2600.0);
	Capture.AdvanceTo(5000.0);
	EXPECT_NEAR(Capture.Sim.GetLaneMixGain(2), 1.0, 1e-9);
	Capture.AdvanceTo(5600.0);
	EXPECT_FALSE(Capture.Sim.GetLane(2).bCaptured);
	EXPECT_NEAR(Capture.Sim.GetLaneMixGain(2), 0.0, 1e-9);

	// Songs can keep a quiet bed of the other instruments; auto-play hears everything.
	FGameRules Quiet;
	Quiet.IdleLaneGain = 0.2f;
	FHarness Bed({MakeNote(0, 60000.0)}, EDifficulty::Normal, 0.0, Quiet);
	EXPECT_NEAR(Bed.Sim.GetLaneMixGain(5), 0.2, 1e-6);
	Bed.Sim.SetAutoPlayAll(true);
	EXPECT_NEAR(Bed.Sim.GetLaneMixGain(5), 1.0, 1e-9);
}

AMP_TEST(EarlyPressMissesAndGhostPressIsFree)
{
	FHarness H({MakeNote(1, 1000.0)});
	H.PressLane(1, 500.0); // 500ms early: outside the early-miss window (400ms), ghost press
	EXPECT_EQ(H.CountEvents(EEventType::GhostPress), 1);
	EXPECT_EQ(H.Sim.GetEnergy(), 50);
	EXPECT_TRUE(H.Sim.GetNotes()[0].IsPending());

	H.PressLane(1, 650.0); // 350ms early: inside the early-miss window, costs the note
	EXPECT_TRUE(H.Sim.GetNotes()[0].Judgement == EJudgement::Miss);
	EXPECT_EQ(H.Sim.GetEnergy(), 47);

	H.PressLane(4, 700.0); // empty lane
	EXPECT_EQ(H.CountEvents(EEventType::GhostPress), 2);
	const FEvent* Ghost = H.LastEvent(EEventType::GhostPress);
	EXPECT_TRUE(Ghost != nullptr && Ghost->Lane == 4 && Ghost->Column == Middle);
}

AMP_TEST(WrongButtonLosesTheGem)
{
	FHarness H({MakeNote(2, 1000.0, Left), MakeNote(2, 3000.0, Right)});
	H.Press(Right, 1020.0); // the left gem is in the window: wrong button
	EXPECT_TRUE(H.Sim.GetNotes()[0].Judgement == EJudgement::Miss);
	EXPECT_EQ(H.Sim.GetEnergy(), 47);
	EXPECT_TRUE(H.Sim.GetNotes()[1].IsPending());

	// Early presses only cost a gem of the same column.
	H.Press(Left, 2650.0);
	EXPECT_TRUE(H.Sim.GetNotes()[1].IsPending());
	EXPECT_EQ(H.CountEvents(EEventType::GhostPress), 1);
	H.Press(Right, 2650.0);
	EXPECT_TRUE(H.Sim.GetNotes()[1].Judgement == EJudgement::Miss);
}

AMP_TEST(ClosestNoteOfTheColumnIsJudged)
{
	FHarness H({MakeNote(0, 1000.0), MakeNote(0, 1200.0), MakeNote(0, 1210.0, Right)});
	H.Move(0, 0.0);
	H.Press(Middle, 1190.0);
	EXPECT_TRUE(H.Sim.GetNotes()[1].Judgement == EJudgement::Perfect);
	EXPECT_TRUE(H.Sim.GetNotes()[0].IsPending());
	H.Press(Right, 1200.0);
	EXPECT_TRUE(H.Sim.GetNotes()[2].Judgement == EJudgement::Perfect);
	H.AdvanceTo(1400.0);
	EXPECT_TRUE(H.Sim.GetNotes()[0].Judgement == EJudgement::Miss);
}

AMP_TEST(ChordsNeedEveryButton)
{
	std::vector<FChartEntry> Chart = {{1, 1000.0, 2, 0b011, ENoteType::Double}, {2, 2000.0, 2, 0b110, ENoteType::Double}};
	FHarness H(ExpandChart(Chart));
	EXPECT_EQ(H.Sim.GetNotes().size(), size_t(4));
	H.Press(Left, 1000.0);
	H.Press(Middle, 1005.0);
	EXPECT_EQ(H.Sim.GetStats().Perfect, 2);

	H.Press(Right, 2000.0); // only half of the second chord
	H.AdvanceTo(2400.0);
	EXPECT_EQ(H.Sim.GetStats().Perfect, 3);
	EXPECT_EQ(H.Sim.GetStats().Miss, 1);
}

AMP_TEST(EnergyIsCappedAndGameOverAtZero)
{
	// 4 hits (+8) and a capture (-5) take energy to 53, then 36 auto-played Perfects (+72) hit the cap.
	FHarness Up(LaneNotes(0, 1000.0, 400.0, 40));
	for (int32_t Index = 0; Index < 40; ++Index)
	{
		Up.PressLane(0, 1000.0 + 400.0 * Index);
	}
	EXPECT_EQ(Up.Sim.GetEnergy(), 100);

	std::vector<FNote> Many;
	for (int32_t Index = 0; Index < 30; ++Index)
	{
		Many.push_back(MakeNote(2, 1000.0 + 200.0 * Index, Index % NumColumns));
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
	Down.Sim.PressColumn(Middle, 10000.0);
	Down.Sim.StepShip(1, 10000.0);
	EXPECT_EQ(Down.Sim.GetScore(), Score);
	EXPECT_EQ(Down.Sim.GetShipLane(), 2);
}

AMP_TEST(PerLaneCombosAndMultiplier)
{
	std::vector<FNote> Notes = LaneNotes(0, 1000.0, 500.0, 3);
	std::vector<FNote> Other = LaneNotes(1, 1250.0, 500.0, 3);
	Notes.insert(Notes.end(), Other.begin(), Other.end());
	FHarness H(Notes);
	H.PressLane(0, 1000.0);
	H.PressLane(1, 1250.0);
	H.PressLane(0, 1500.0);
	EXPECT_EQ(H.Sim.GetLane(0).Combo, 2);
	EXPECT_EQ(H.Sim.GetLane(1).Combo, 1);
	H.AdvanceTo(2100.0); // lane 1 gem at 1750 passes while the ship is in lane 0: skipped, combo kept
	EXPECT_EQ(H.Sim.GetLane(1).Combo, 1);
	EXPECT_EQ(H.Sim.GetLane(1).CaptureStreak, 0);
	H.AdvanceTo(2400.0); // lane 0 gem at 2000 is missed
	EXPECT_EQ(H.Sim.GetLane(0).Combo, 0);
	EXPECT_EQ(H.Sim.GetLane(1).Combo, 1);
}

AMP_TEST(ComboMultiplierAppliesAtThreshold)
{
	// On Mellow 1.2x starts at a combo of 3, below the capture threshold of 4, so the tier shows up before any auto-play.
	FHarness H(LaneNotes(0, 1000.0, 500.0, 3), EDifficulty::Mellow);
	H.PressLane(0, 1000.0);
	H.PressLane(0, 1500.0);
	EXPECT_EQ(H.Sim.GetScore(), 20);
	H.PressLane(0, 2000.0);
	EXPECT_EQ(H.Sim.GetScore(), 32); // third hit: 10 x 1.2
}

AMP_TEST(LaneCaptureAfterFourHitsAndAutoAdvance)
{
	FHarness H(LaneNotes(2, 1000.0, 500.0, 12));
	for (int32_t Index = 0; Index < 3; ++Index)
	{
		H.Press(Middle, 1000.0 + 500.0 * Index);
	}
	EXPECT_FALSE(H.Sim.GetLane(2).bCaptured);
	EXPECT_EQ(H.Sim.GetEnergy(), 56);

	H.Press(Middle, 2500.0);
	EXPECT_TRUE(H.Sim.GetLane(2).bCaptured);
	EXPECT_EQ(H.CountEvents(EEventType::LaneCaptured), 1);
	EXPECT_EQ(H.Sim.GetEnergy(), 56 + 2 - 5);
	EXPECT_NEAR(H.Sim.GetCaptureRemainingMs(2), 30000.0, 1e-6);

	// The Beat Blaster jumps on to the next free lane (to the right when nothing else is playing).
	EXPECT_EQ(H.Sim.GetShipLane(), 3);
	const FEvent* Moved = H.LastEvent(EEventType::ShipMoved);
	EXPECT_TRUE(Moved != nullptr && Moved->bAuto && Moved->Count == 2);

	// The remaining notes play themselves as Perfect hits and keep the combo going.
	H.AdvanceTo(7000.0);
	EXPECT_EQ(H.Sim.GetStats().AutoHits, 8);
	EXPECT_EQ(H.Sim.GetLane(2).Combo, 12);
	EXPECT_EQ(H.Sim.GetEnergy(), 53 + 16);
	EXPECT_EQ(H.Sim.GetStats().Miss, 0);
	EXPECT_EQ(H.Sim.GetStats().Skipped, 0);
}

AMP_TEST(AutoAdvancePrefersLanesWithMusic)
{
	std::vector<FNote> Notes = LaneNotes(2, 1000.0, 500.0, 4);
	std::vector<FNote> Left1 = LaneNotes(1, 4000.0, 500.0, 4);
	std::vector<FNote> Right4 = LaneNotes(4, 3000.0, 500.0, 4);
	Notes.insert(Notes.end(), Left1.begin(), Left1.end());
	Notes.insert(Notes.end(), Right4.begin(), Right4.end());

	FHarness H(Notes);
	for (int32_t Index = 0; Index < 4; ++Index)
	{
		H.Press(Middle, 1000.0 + 500.0 * Index);
	}
	EXPECT_EQ(H.Sim.GetShipLane(), 1); // lane 3 is silent, lane 1 is the nearest with notes

	FGameRules Rules;
	Rules.bAutoAdvanceOnCapture = false;
	FHarness Stay(Notes, EDifficulty::Normal, 0.0, Rules);
	for (int32_t Index = 0; Index < 4; ++Index)
	{
		Stay.Press(Middle, 1000.0 + 500.0 * Index);
	}
	EXPECT_TRUE(Stay.Sim.GetLane(2).bCaptured);
	EXPECT_EQ(Stay.Sim.GetShipLane(), 2);
}

AMP_TEST(CaptureExpiresAndNeedsFreshStreak)
{
	std::vector<FNote> Notes = LaneNotes(4, 1000.0, 500.0, 4);
	Notes.push_back(MakeNote(4, 32000.0)); // inside the capture: auto-played
	Notes.push_back(MakeNote(4, 33000.0)); // after it expires (2500 + 30000): the player must hit it
	FHarness H(Notes);
	for (int32_t Index = 0; Index < 4; ++Index)
	{
		H.PressLane(4, 1000.0 + 500.0 * Index);
	}
	EXPECT_TRUE(H.Sim.GetLane(4).bCaptured);
	EXPECT_EQ(H.Sim.GetShipLane(), 5);
	H.AdvanceTo(32600.0);
	EXPECT_FALSE(H.Sim.GetLane(4).bCaptured);
	EXPECT_EQ(H.CountEvents(EEventType::CaptureExpired), 1);
	EXPECT_TRUE(H.Sim.GetNotes()[4].bAutoPlayed);
	EXPECT_TRUE(H.Sim.GetNotes()[5].IsPending());
	EXPECT_EQ(H.Sim.GetLane(4).CaptureStreak, 0);
	H.Move(4, 32700.0);
	H.AdvanceTo(33400.0);
	EXPECT_TRUE(H.Sim.GetNotes()[5].Judgement == EJudgement::Miss);
}

AMP_TEST(MissOrSkipBreaksCaptureStreak)
{
	FHarness H(LaneNotes(0, 1000.0, 500.0, 8));
	H.Move(0, 0.0);
	H.Press(Middle, 1000.0);
	H.Press(Middle, 1500.0);
	H.Press(Middle, 2000.0);
	H.AdvanceTo(2900.0); // miss the 4th note
	EXPECT_EQ(H.Sim.GetLane(0).CaptureStreak, 0);
	H.Press(Middle, 3000.0);
	H.Press(Middle, 3500.0);
	H.Press(Middle, 4000.0);
	EXPECT_FALSE(H.Sim.GetLane(0).bCaptured);
	H.Press(Middle, 4500.0);
	EXPECT_TRUE(H.Sim.GetLane(0).bCaptured);

	// Leaving the lane mid-phrase: the skipped gem resets the streak.
	FHarness Away(LaneNotes(0, 1000.0, 500.0, 8));
	Away.PressLane(0, 1000.0);
	Away.PressLane(0, 1500.0);
	Away.PressLane(0, 2000.0);
	Away.Move(1, 2200.0);
	Away.PressLane(0, 3000.0); // 2500 was skipped
	EXPECT_FALSE(Away.Sim.GetLane(0).bCaptured);
	EXPECT_EQ(Away.Sim.GetLane(0).CaptureStreak, 1);
	EXPECT_EQ(Away.Sim.GetLane(0).Combo, 4);
}

AMP_TEST(CaptureCostsEnergyAtLowEnergy)
{
	// Burn energy down to 5 with misses, then build a streak.
	std::vector<FNote> Notes;
	for (int32_t Index = 0; Index < 15; ++Index)
	{
		Notes.push_back(MakeNote(2, 1000.0 + 100.0 * Index, Index % NumColumns));
	}
	std::vector<FNote> Streak = LaneNotes(2, 5000.0, 500.0, 4);
	Notes.insert(Notes.end(), Streak.begin(), Streak.end());
	FHarness H(Notes);
	H.AdvanceTo(4000.0);
	EXPECT_EQ(H.Sim.GetEnergy(), 5); // 50 - 15 * 3
	for (int32_t Index = 0; Index < 4; ++Index)
	{
		H.Press(Middle, 5000.0 + 500.0 * Index);
	}
	// 5 + 4 * 2 = 13 > 5: the capture is affordable and its cost is paid.
	EXPECT_TRUE(H.Sim.GetLane(2).bCaptured);
	EXPECT_EQ(H.Sim.GetEnergy(), 8);
}

AMP_TEST(CaptureDeniedWhenUnaffordable)
{
	// With a 60 energy cost, a capture needs more than 60 energy; the streak keeps counting until then.
	FGameRules Rules;
	Rules.CaptureEnergyCost = 60;
	FHarness H(LaneNotes(2, 1000.0, 500.0, 8), EDifficulty::Normal, 0.0, Rules);
	for (int32_t Index = 0; Index < 5; ++Index)
	{
		H.Press(Middle, 1000.0 + 500.0 * Index);
	}
	EXPECT_FALSE(H.Sim.GetLane(2).bCaptured);
	EXPECT_EQ(H.Sim.GetLane(2).CaptureStreak, 5);
	EXPECT_EQ(H.Sim.GetEnergy(), 60);
	H.Press(Middle, 3500.0);
	EXPECT_TRUE(H.Sim.GetLane(2).bCaptured);
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
	H.Move(5, 0.0);
	H.AdvanceTo(2400.0); // 3 misses
	EXPECT_FALSE(H.Sim.GetLane(5).bMuted);
	H.AdvanceTo(2900.0); // 4th miss
	EXPECT_TRUE(H.Sim.GetLane(5).bMuted);
	EXPECT_EQ(H.Sim.GetLane(5).MuteCount, 1);
	EXPECT_EQ(H.CountEvents(EEventType::LaneMuted), 1);
	EXPECT_EQ(H.Sim.GetLane(5).ConsecutiveMisses, 4);

	H.Press(Middle, 3500.0);
	EXPECT_FALSE(H.Sim.GetLane(5).bMuted);
	EXPECT_EQ(H.CountEvents(EEventType::LaneUnmuted), 1);
	EXPECT_EQ(H.Sim.GetLane(5).ConsecutiveMisses, 0);
	EXPECT_TRUE(H.Sim.GetScore() > 0); // muted lanes still score
}

AMP_TEST(CapturedLanePressDoesNothing)
{
	FHarness H(LaneNotes(2, 1000.0, 500.0, 6));
	for (int32_t Index = 0; Index < 4; ++Index)
	{
		H.Press(Middle, 1000.0 + 500.0 * Index);
	}
	H.Move(2, 2600.0);
	H.Press(Middle, 2700.0); // captured: no ghost, no early miss
	EXPECT_EQ(H.CountEvents(EEventType::GhostPress), 0);
	EXPECT_TRUE(H.Sim.GetNotes()[4].IsPending());
	H.AdvanceTo(3000.0);
	EXPECT_TRUE(H.Sim.GetNotes()[4].bAutoPlayed);
}

AMP_TEST(Score2xStacksMultiplicatively)
{
	FHarness H({MakeNote(0, 1000.0), MakeNote(1, 1500.0), MakeNote(2, 20000.0)});
	H.Sim.ApplyPowerup(EPowerupType::Score2x);
	H.PressLane(0, 1000.0);
	EXPECT_EQ(H.Sim.GetScore(), 20);
	H.Sim.ApplyPowerup(EPowerupType::Score2x);
	EXPECT_NEAR(H.Sim.GetGlobalMultiplier(), 4.0, 1e-9);
	H.PressLane(1, 1500.0);
	EXPECT_EQ(H.Sim.GetScore(), 60);
	H.AdvanceTo(17000.0); // both 15s timers (real time) expire
	EXPECT_EQ(H.Sim.GetScore2xStacks(), 0);
	EXPECT_EQ(H.CountEvents(EEventType::PowerupEffectEnded), 2);
	H.PressLane(2, 20000.0);
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
	H.PressLane(0, 1000.0);
	EXPECT_EQ(H.Sim.GetScore(), 15);
	EXPECT_NEAR(H.Sim.GetGlobalMultiplier(), 1.6, 1e-9);
	for (int32_t Index = 1; Index < 20; ++Index)
	{
		H.PressLane(Index % 3, 1000.0 + 300.0 * Index);
	}
	EXPECT_NEAR(H.Sim.GetGlobalMultiplier(), 3.0, 1e-9); // capped
	H.Move(5, 7500.0);
	H.AdvanceTo(8400.0 + 100.0); // miss the lane 5 note
	EXPECT_FALSE(H.Sim.IsFeverActive());
	EXPECT_NEAR(H.Sim.GetGlobalMultiplier(), 1.0, 1e-9);
	EXPECT_EQ(H.CountEvents(EEventType::FeverBroken), 1);
}

AMP_TEST(ShieldAbsorbsOneMiss)
{
	FHarness H({MakeNote(0, 1000.0), MakeNote(1, 2000.0)});
	H.Sim.ApplyPowerup(EPowerupType::Shield);
	H.Move(0, 0.0);
	H.AdvanceTo(1400.0);
	EXPECT_EQ(H.Sim.GetEnergy(), 50);
	EXPECT_FALSE(H.Sim.IsShieldActive());
	EXPECT_EQ(H.CountEvents(EEventType::ShieldAbsorbedMiss), 1);
	EXPECT_EQ(H.Sim.GetLane(0).Combo, 0); // the miss still counts
	H.Move(1, 1500.0);
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

AMP_TEST(LaneCleanerClearsTheShipLane)
{
	std::vector<FNote> Notes = LaneNotes(3, 2000.0, 250.0, 6);
	Notes.push_back(MakeNote(1, 2100.0));
	FHarness H(Notes);
	H.AdvanceTo(1000.0);
	H.Sim.ApplyPowerup(EPowerupType::LaneCleaner);
	EXPECT_TRUE(H.Sim.IsLaneCleanerArmed());
	H.Move(3, 1000.0); // moving picks the target...
	EXPECT_TRUE(H.Sim.IsLaneCleanerArmed());
	H.Press(Left, 1000.0); // ...any gem button fires the cleaner
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
	EXPECT_EQ(H.Sim.GetShipLane(), 4); // the ship's own lane was captured, so it moves on
}

AMP_TEST(PowerupsAreCollectedByBeingInTheirLane)
{
	FHarness H({MakeNote(0, 60000.0)});
	H.Sim.SpawnPowerup(EPowerupType::Shield, 2, 3000.0);
	H.AdvanceTo(2700.0);
	EXPECT_EQ(H.Sim.GetPowerups().size(), size_t(1));
	H.AdvanceTo(2760.0); // within 250ms of its arrival, with the ship in its lane
	EXPECT_EQ(H.Sim.GetPowerups().size(), size_t(0));
	EXPECT_EQ(H.Sim.GetScore(), 500);
	EXPECT_TRUE(H.Sim.IsShieldActive());
	EXPECT_EQ(H.Sim.GetStats().PowerupsCollected, 1);

	FHarness Missed({MakeNote(0, 60000.0)});
	Missed.Sim.SpawnPowerup(EPowerupType::Fever, 4, 3000.0);
	Missed.AdvanceTo(3240.0);
	EXPECT_EQ(Missed.Sim.GetPowerups().size(), size_t(1));
	Missed.AdvanceTo(3260.0);
	EXPECT_EQ(Missed.Sim.GetPowerups().size(), size_t(0));
	EXPECT_EQ(Missed.CountEvents(EEventType::PowerupDespawned), 1);
	EXPECT_EQ(Missed.Sim.GetScore(), 0);

	// Jumping in just after it arrives still catches it...
	FHarness Late({MakeNote(0, 60000.0)});
	Late.Sim.SpawnPowerup(EPowerupType::SlowMotion, 5, 3000.0);
	Late.Move(5, 3100.0);
	Late.AdvanceTo(3110.0);
	EXPECT_TRUE(Late.Sim.IsSlowMotionActive());

	// ...and so does passing through its lane inside the window.
	FHarness Pass({MakeNote(0, 60000.0)});
	Pass.Sim.SpawnPowerup(EPowerupType::Score2x, 5, 3000.0);
	Pass.Move(5, 2700.0);
	Pass.Move(4, 2800.0);
	Pass.AdvanceTo(3300.0);
	EXPECT_EQ(Pass.Sim.GetScore2xStacks(), 1);

	// Spawned powerups start at the far end of the lane: one approach time away.
	FHarness Spawn({MakeNote(0, 60000.0)});
	Spawn.AdvanceTo(1000.0);
	Spawn.Sim.ForceSpawnPowerup(EPowerupType::Fever, 1);
	EXPECT_NEAR(Spawn.Sim.GetPowerups()[0].ArrivalMs, 3000.0, 1e-9);
}

AMP_TEST(PowerupsSpawnOnSchedule)
{
	FSimulation Sim;
	Sim.Start({MakeNote(0, 600000.0)}, GetDefaultDifficultyParams(EDifficulty::Normal), FGameRules(), 0.0, 99);
	Sim.MoveShip(0, 0.0);
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
	Notes.push_back(MakeNote(5, 2500.0));
	FHarness H(Notes, EDifficulty::Normal, 5000.0);
	H.PressLane(0, 1000.0);
	H.PressLane(0, 1650.0); // Good
	H.Move(3, 1800.0);      // in front of the lane 3 gem, but never fires
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
	EXPECT_EQ(Summary.Skipped, 1);
	EXPECT_EQ(Summary.TotalNotes, 4);
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
		Notes.push_back(MakeNote(Index % NumLanes, 500.0 + 150.0 * Index, Index % NumColumns));
	}
	FHarness H(Notes);
	H.Sim.SetAutoPlayAll(true);
	H.AdvanceTo(10000.0);
	EXPECT_TRUE(H.Sim.IsComplete());
	EXPECT_EQ(H.Sim.GetStats().AutoHits, 30);
	EXPECT_EQ(H.Sim.GetStats().Miss, 0);
	EXPECT_EQ(H.Sim.GetStats().Skipped, 0);
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
