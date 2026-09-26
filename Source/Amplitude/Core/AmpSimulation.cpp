#include "Core/AmpSimulation.h"

#include "Core/AmpRules.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Amp
{
	void FSimulation::Start(std::vector<FNote> InNotes, const FDifficultyParams& InParams, const FGameRules& InRules, double InSongLengthMs, uint64_t Seed, double StartSongTimeMs)
	{
		Notes = std::move(InNotes);
		std::stable_sort(Notes.begin(), Notes.end(), [](const FNote& A, const FNote& B)
		{
			return A.TimeMs < B.TimeMs || (A.TimeMs == B.TimeMs && A.Lane < B.Lane);
		});

		Params = InParams;
		Rules = InRules;

		Lanes = {};
		PendingNotes = 0;
		for (size_t Index = 0; Index < Notes.size(); ++Index)
		{
			FNote& Note = Notes[Index];
			Note.Judgement = EJudgement::None;
			Note.bAutoPlayed = false;
			Note.bCleared = false;
			if (Note.Lane < 0 || Note.Lane >= NumLanes)
			{
				// Unplayable note: resolve it up front so it never blocks completion.
				Note.bCleared = true;
				continue;
			}
			Lanes[static_cast<size_t>(Note.Lane)].NoteIndices.push_back(static_cast<int32_t>(Index));
			++PendingNotes;
		}

		const double LastNoteEndMs = Notes.empty() ? 0.0 : Notes.back().TimeMs + Params.GoodWindowMs + 500.0;
		SongLengthMs = std::max(InSongLengthMs, LastNoteEndMs);

		Powerups.clear();
		Events.clear();
		Effects = FActiveEffects();
		Stats = FRunStats();
		Random.Reset(Seed);

		Score = 0;
		Energy = std::clamp(Rules.StartingEnergy, 0, Rules.MaxEnergy);
		ShipLane = 2;
		NextPowerupId = 1;
		SongTimeMs = StartSongTimeMs;
		RealTimeMs = 0.0;
		bGameOver = false;
		bComplete = false;
		ScheduleNextPowerup();
	}

	void FSimulation::PressLane(int32_t Lane, double InSongTimeMs)
	{
		if (IsFinished() || Lane < 0 || Lane >= NumLanes)
		{
			return;
		}

		// Sampled before moving: moving may collect a new Lane Cleaner, which must wait for the next press.
		const bool bChoosingCleanerLane = IsLaneCleanerArmed();
		MoveShip(Lane);

		if (bChoosingCleanerLane)
		{
			ClearLane(Lane, InSongTimeMs);
			return;
		}

		FLaneState& LaneState = Lanes[static_cast<size_t>(Lane)];
		if (LaneState.bCaptured || bAutoPlayAll)
		{
			// Auto-play owns this lane; pressing only moves the ship.
			return;
		}

		AdvanceCursor(LaneState);

		// Pick the pending note closest to the press inside the good window.
		int32_t BestIndex = -1;
		double BestError = std::numeric_limits<double>::max();
		for (size_t Slot = LaneState.Cursor; Slot < LaneState.NoteIndices.size(); ++Slot)
		{
			const int32_t NoteIndex = LaneState.NoteIndices[Slot];
			const FNote& Note = Notes[static_cast<size_t>(NoteIndex)];
			if (!Note.IsPending())
			{
				continue;
			}
			const double Offset = InSongTimeMs - Note.TimeMs;
			if (Offset < -Params.GoodWindowMs)
			{
				break;
			}
			const double Error = std::abs(Offset);
			if (Error <= Params.GoodWindowMs && Error < BestError)
			{
				BestIndex = NoteIndex;
				BestError = Error;
			}
		}

		if (BestIndex >= 0)
		{
			const FNote& Note = Notes[static_cast<size_t>(BestIndex)];
			ResolveHit(BestIndex, JudgeTimingError(Params, BestError), InSongTimeMs - Note.TimeMs, InSongTimeMs, false);
			return;
		}

		// No note in the good window: a press just before the next note counts as missing it,
		// anything further away is a harmless ghost press (the player is lane hopping).
		for (size_t Slot = LaneState.Cursor; Slot < LaneState.NoteIndices.size(); ++Slot)
		{
			const int32_t NoteIndex = LaneState.NoteIndices[Slot];
			const FNote& Note = Notes[static_cast<size_t>(NoteIndex)];
			if (!Note.IsPending() || Note.TimeMs <= InSongTimeMs)
			{
				continue;
			}
			if (Note.TimeMs - InSongTimeMs <= Params.EarlyMissWindowMs)
			{
				ResolveMiss(NoteIndex, InSongTimeMs - Note.TimeMs, InSongTimeMs);
				return;
			}
			break;
		}

		++Stats.GhostPresses;
		FEvent Event;
		Event.Type = EEventType::GhostPress;
		Event.Lane = Lane;
		Event.SongTimeMs = InSongTimeMs;
		Emit(Event);
	}

	void FSimulation::MoveShip(int32_t Lane)
	{
		if (IsFinished() || Lane < 0 || Lane >= NumLanes)
		{
			return;
		}
		if (Lane != ShipLane)
		{
			FEvent Event;
			Event.Type = EEventType::ShipMoved;
			Event.Lane = Lane;
			Event.Count = ShipLane;
			Event.SongTimeMs = SongTimeMs;
			ShipLane = Lane;
			Emit(Event);
		}
		CollectPowerupsAtShip();
	}

	void FSimulation::Advance(double InSongTimeMs, double RealDeltaMs)
	{
		if (IsFinished())
		{
			return;
		}

		SongTimeMs = InSongTimeMs;
		const double SafeDelta = std::max(0.0, RealDeltaMs);
		RealTimeMs += SafeDelta;

		UpdateEffects(SafeDelta);
		ProcessDueNotes(SongTimeMs);
		if (IsFinished())
		{
			return;
		}
		UpdateCaptures(SongTimeMs);
		UpdatePowerups(SafeDelta, SongTimeMs);
		CheckCompletion(SongTimeMs);
	}

	void FSimulation::DrainEvents(std::vector<FEvent>& Out)
	{
		Out.insert(Out.end(), Events.begin(), Events.end());
		Events.clear();
	}

	int32_t FSimulation::ForceSpawnPowerup(EPowerupType Type, int32_t Lane)
	{
		FFallingPowerup Powerup;
		Powerup.Id = NextPowerupId++;
		Powerup.Type = Type;
		Powerup.Lane = std::clamp(Lane, 0, NumLanes - 1);
		Powerups.push_back(Powerup);

		FEvent Event;
		Event.Type = EEventType::PowerupSpawned;
		Event.Lane = Powerup.Lane;
		Event.Powerup = Type;
		Event.PowerupId = Powerup.Id;
		Event.SongTimeMs = SongTimeMs;
		Emit(Event);
		return Powerup.Id;
	}

	void FSimulation::ApplyPowerup(EPowerupType Type)
	{
		switch (Type)
		{
		case EPowerupType::Score2x:
			// Each pickup is an independent 15s timer; overlapping pickups stack multiplicatively.
			Effects.Score2xRemainingMs.push_back(Rules.Score2xDurationMs);
			break;

		case EPowerupType::LaneCleaner:
		{
			Effects.LaneCleanerRemainingMs = Rules.LaneCleanerSelectTimeoutMs;
			FEvent Event;
			Event.Type = EEventType::LaneCleanerArmed;
			Event.Powerup = Type;
			Event.SongTimeMs = SongTimeMs;
			Emit(Event);
			break;
		}

		case EPowerupType::SlowMotion:
			Effects.SlowMotionRemainingMs = Rules.SlowMotionDurationMs;
			break;

		case EPowerupType::Shield:
			Effects.ShieldRemainingMs = Rules.ShieldDurationMs;
			break;

		case EPowerupType::Fever:
			if (!IsFeverActive())
			{
				Effects.FeverMultiplier = Rules.FeverStartMultiplier;
			}
			Effects.FeverRemainingMs = Rules.FeverDurationMs;
			break;

		case EPowerupType::AutoCapture:
		{
			const int32_t Lane = PickAutoCaptureLane();
			if (Lane >= 0 && TryCapture(Lane, SongTimeMs))
			{
				Lanes[static_cast<size_t>(Lane)].CaptureStreak = 0;
			}
			else
			{
				FEvent Event;
				Event.Type = EEventType::AutoCaptureFailed;
				Event.Lane = Lane;
				Event.Powerup = Type;
				Event.SongTimeMs = SongTimeMs;
				Emit(Event);
			}
			break;
		}
		}
	}

	double FSimulation::GetPlaybackRate() const
	{
		return IsSlowMotionActive() ? Rules.SlowMotionRate : 1.0;
	}

	double FSimulation::GetGlobalMultiplier() const
	{
		double Multiplier = std::pow(2.0, static_cast<double>(Effects.Score2xRemainingMs.size()));
		if (IsFeverActive())
		{
			Multiplier *= Effects.FeverMultiplier;
		}
		return Multiplier;
	}

	double FSimulation::GetLaneComboMultiplier(int32_t Lane) const
	{
		if (Lane < 0 || Lane >= NumLanes)
		{
			return 1.0;
		}
		return GetComboMultiplier(Params, Lanes[static_cast<size_t>(Lane)].Combo);
	}

	double FSimulation::GetCaptureRemainingMs(int32_t Lane) const
	{
		if (Lane < 0 || Lane >= NumLanes || !Lanes[static_cast<size_t>(Lane)].bCaptured)
		{
			return 0.0;
		}
		return std::max(0.0, Lanes[static_cast<size_t>(Lane)].CaptureEndMs - SongTimeMs);
	}

	double FSimulation::GetPowerupY(const FFallingPowerup& Powerup) const
	{
		return Rules.PowerupLifetimeMs > 0.0 ? Powerup.AgeMs / Rules.PowerupLifetimeMs : 1.0;
	}

	FRunSummary FSimulation::Summarize() const
	{
		FRunSummary Summary;
		Summary.Score = Score;
		Summary.Perfect = Stats.Perfect;
		Summary.Good = Stats.Good;
		Summary.Miss = Stats.Miss;
		Summary.AutoHits = Stats.AutoHits;
		Summary.NotesCleared = Stats.NotesCleared;
		Summary.TotalNotes = static_cast<int32_t>(Notes.size());
		Summary.AccuracyPercent = ComputeAccuracyPercent(Stats.Perfect + Stats.AutoHits, Stats.Good, Stats.Miss);
		Summary.BestCombo = Stats.BestCombo;
		Summary.Captures = Stats.Captures;
		Summary.PowerupsCollected = Stats.PowerupsCollected;
		Summary.bCompleted = bComplete;
		Summary.bGameOver = bGameOver;

		for (int32_t Lane = 0; Lane < NumLanes; ++Lane)
		{
			const FLaneState& State = Lanes[static_cast<size_t>(Lane)];
			if (State.MaxCombo > Summary.BestLaneCombo)
			{
				Summary.BestLane = Lane;
				Summary.BestLaneCombo = State.MaxCombo;
			}
			const bool bWorse = State.MuteCount > Summary.WorstLaneMutes
				|| (State.MuteCount == Summary.WorstLaneMutes && State.Misses > Summary.WorstLaneMisses);
			if (bWorse)
			{
				Summary.WorstLane = Lane;
				Summary.WorstLaneMutes = State.MuteCount;
				Summary.WorstLaneMisses = State.Misses;
			}
		}
		return Summary;
	}

	void FSimulation::AdvanceCursor(FLaneState& Lane)
	{
		while (Lane.Cursor < Lane.NoteIndices.size() && !Notes[static_cast<size_t>(Lane.NoteIndices[Lane.Cursor])].IsPending())
		{
			++Lane.Cursor;
		}
	}

	void FSimulation::ResolveHit(int32_t NoteIndex, EJudgement Judgement, double OffsetMs, double AtMs, bool bAuto)
	{
		FNote& Note = Notes[static_cast<size_t>(NoteIndex)];
		Note.Judgement = Judgement;
		Note.bAutoPlayed = bAuto;
		Note.HitOffsetMs = OffsetMs;
		Note.ResolvedAtMs = AtMs;
		--PendingNotes;

		FLaneState& Lane = Lanes[static_cast<size_t>(Note.Lane)];
		++Lane.Combo;
		++Lane.Hits;
		Lane.MaxCombo = std::max(Lane.MaxCombo, Lane.Combo);
		Lane.ConsecutiveMisses = 0;
		Stats.BestCombo = std::max(Stats.BestCombo, Lane.Combo);

		if (bAuto)
		{
			++Stats.AutoHits;
		}
		else if (Judgement == EJudgement::Perfect)
		{
			++Stats.Perfect;
		}
		else
		{
			++Stats.Good;
		}

		const bool bPerfect = Judgement == EJudgement::Perfect;
		const double ComboMultiplier = GetComboMultiplier(Params, Lane.Combo);
		const double GlobalMultiplier = GetGlobalMultiplier();
		const int64_t Points = ComputeHitScore(bPerfect ? Rules.PerfectPoints : Rules.GoodPoints, ComboMultiplier, GlobalMultiplier);
		Score += Points;
		AddEnergy(bPerfect ? Params.EnergyOnPerfect : Params.EnergyOnGood);

		if (IsFeverActive())
		{
			Effects.FeverMultiplier = std::min(Rules.FeverMaxMultiplier, Effects.FeverMultiplier + Rules.FeverStepPerHit);
		}

		if (Lane.bMuted)
		{
			Lane.bMuted = false;
			FEvent Unmuted;
			Unmuted.Type = EEventType::LaneUnmuted;
			Unmuted.Lane = Note.Lane;
			Unmuted.SongTimeMs = AtMs;
			Emit(Unmuted);
		}

		FEvent Event;
		Event.Type = EEventType::NoteHit;
		Event.Lane = Note.Lane;
		Event.Judgement = Judgement;
		Event.bAuto = bAuto;
		Event.NoteIndex = NoteIndex;
		Event.Count = Lane.Combo;
		Event.Points = Points;
		Event.Multiplier = ComboMultiplier * GlobalMultiplier;
		Event.OffsetMs = OffsetMs;
		Event.SongTimeMs = AtMs;
		Emit(Event);

		if (!bAuto && !Lane.bCaptured)
		{
			++Lane.CaptureStreak;
			if (Lane.CaptureStreak >= Rules.CaptureStreak && TryCapture(Note.Lane, AtMs))
			{
				Lane.CaptureStreak = 0;
			}
		}
	}

	void FSimulation::ResolveMiss(int32_t NoteIndex, double OffsetMs, double AtMs)
	{
		FNote& Note = Notes[static_cast<size_t>(NoteIndex)];
		Note.Judgement = EJudgement::Miss;
		Note.HitOffsetMs = OffsetMs;
		Note.ResolvedAtMs = AtMs;
		--PendingNotes;

		FLaneState& Lane = Lanes[static_cast<size_t>(Note.Lane)];
		Lane.Combo = 0;
		Lane.CaptureStreak = 0;
		++Lane.ConsecutiveMisses;
		++Lane.Misses;
		++Stats.Miss;

		FEvent Event;
		Event.Type = EEventType::NoteMissed;
		Event.Lane = Note.Lane;
		Event.Judgement = EJudgement::Miss;
		Event.NoteIndex = NoteIndex;
		Event.Count = Lane.ConsecutiveMisses;
		Event.OffsetMs = OffsetMs;
		Event.SongTimeMs = AtMs;
		Emit(Event);

		if (IsShieldActive())
		{
			Effects.ShieldRemainingMs = 0.0;
			FEvent Shield;
			Shield.Type = EEventType::ShieldAbsorbedMiss;
			Shield.Lane = Note.Lane;
			Shield.Powerup = EPowerupType::Shield;
			Shield.SongTimeMs = AtMs;
			Emit(Shield);
		}
		else
		{
			AddEnergy(Params.EnergyOnMiss);
		}

		if (IsFeverActive())
		{
			Effects.FeverRemainingMs = 0.0;
			Effects.FeverMultiplier = 1.0;
			FEvent Fever;
			Fever.Type = EEventType::FeverBroken;
			Fever.Lane = Note.Lane;
			Fever.Powerup = EPowerupType::Fever;
			Fever.SongTimeMs = AtMs;
			Emit(Fever);
		}

		if (!Lane.bMuted && Lane.ConsecutiveMisses >= Rules.MuteMissStreak)
		{
			Lane.bMuted = true;
			++Lane.MuteCount;
			FEvent Muted;
			Muted.Type = EEventType::LaneMuted;
			Muted.Lane = Note.Lane;
			Muted.SongTimeMs = AtMs;
			Emit(Muted);
		}

		if (Energy <= 0 && !bGameOver)
		{
			bGameOver = true;
			FEvent Over;
			Over.Type = EEventType::GameOver;
			Over.SongTimeMs = AtMs;
			Emit(Over);
		}
	}

	void FSimulation::AddEnergy(int32_t Delta)
	{
		const bool bWasLow = IsEnergyLow();
		Energy = std::clamp(Energy + Delta, 0, Rules.MaxEnergy);
		if (!bWasLow && IsEnergyLow())
		{
			FEvent Event;
			Event.Type = EEventType::EnergyLow;
			Event.Count = Energy;
			Event.SongTimeMs = SongTimeMs;
			Emit(Event);
		}
	}

	bool FSimulation::TryCapture(int32_t Lane, double AtMs)
	{
		FLaneState& State = Lanes[static_cast<size_t>(Lane)];
		// A capture may never be what ends the run, so it needs more energy than it costs.
		if (State.bCaptured || Energy <= Rules.CaptureEnergyCost)
		{
			return false;
		}

		State.bCaptured = true;
		State.CaptureEndMs = AtMs + Rules.CaptureDurationMs;
		++State.Captures;
		++Stats.Captures;
		AddEnergy(-Rules.CaptureEnergyCost);

		FEvent Event;
		Event.Type = EEventType::LaneCaptured;
		Event.Lane = Lane;
		Event.Count = Rules.CaptureEnergyCost;
		Event.SongTimeMs = AtMs;
		Emit(Event);
		return true;
	}

	void FSimulation::ProcessDueNotes(double NowMs)
	{
		DueScratch.clear();
		for (int32_t LaneIndex = 0; LaneIndex < NumLanes; ++LaneIndex)
		{
			FLaneState& Lane = Lanes[static_cast<size_t>(LaneIndex)];
			AdvanceCursor(Lane);
			for (size_t Slot = Lane.Cursor; Slot < Lane.NoteIndices.size(); ++Slot)
			{
				const int32_t NoteIndex = Lane.NoteIndices[Slot];
				const FNote& Note = Notes[static_cast<size_t>(NoteIndex)];
				if (!Note.IsPending())
				{
					continue;
				}

				const bool bAutoPlayed = bAutoPlayAll || (Lane.bCaptured && Note.TimeMs < Lane.CaptureEndMs);
				if (bAutoPlayed)
				{
					if (Note.TimeMs > NowMs)
					{
						break;
					}
					DueScratch.push_back({Note.TimeMs, NoteIndex, true});
				}
				else
				{
					const double DeadlineMs = Note.TimeMs + Params.GoodWindowMs;
					if (DeadlineMs >= NowMs)
					{
						break;
					}
					DueScratch.push_back({DeadlineMs, NoteIndex, false});
				}
			}
		}

		// Resolve in chronological order so energy (and a possible game over) evolves exactly as it would have in real time.
		std::sort(DueScratch.begin(), DueScratch.end(), [](const FDueNote& A, const FDueNote& B)
		{
			return A.TimeMs < B.TimeMs || (A.TimeMs == B.TimeMs && A.NoteIndex < B.NoteIndex);
		});

		for (const FDueNote& Due : DueScratch)
		{
			if (IsFinished())
			{
				break;
			}
			const FNote& Note = Notes[static_cast<size_t>(Due.NoteIndex)];
			if (!Note.IsPending())
			{
				continue;
			}
			if (Due.bAutoHit)
			{
				ResolveHit(Due.NoteIndex, EJudgement::Perfect, 0.0, Note.TimeMs, true);
			}
			else
			{
				ResolveMiss(Due.NoteIndex, NowMs - Note.TimeMs, Due.TimeMs);
			}
		}
	}

	void FSimulation::UpdateCaptures(double NowMs)
	{
		for (int32_t LaneIndex = 0; LaneIndex < NumLanes; ++LaneIndex)
		{
			FLaneState& Lane = Lanes[static_cast<size_t>(LaneIndex)];
			if (Lane.bCaptured && NowMs >= Lane.CaptureEndMs)
			{
				Lane.bCaptured = false;
				Lane.CaptureStreak = 0;
				FEvent Event;
				Event.Type = EEventType::CaptureExpired;
				Event.Lane = LaneIndex;
				Event.SongTimeMs = Lane.CaptureEndMs;
				Emit(Event);
			}
		}
	}

	void FSimulation::UpdateEffects(double RealDeltaMs)
	{
		auto EmitEnded = [this](EPowerupType Type)
		{
			FEvent Event;
			Event.Type = EEventType::PowerupEffectEnded;
			Event.Powerup = Type;
			Event.SongTimeMs = SongTimeMs;
			Emit(Event);
		};

		for (size_t Index = 0; Index < Effects.Score2xRemainingMs.size();)
		{
			Effects.Score2xRemainingMs[Index] -= RealDeltaMs;
			if (Effects.Score2xRemainingMs[Index] <= 0.0)
			{
				Effects.Score2xRemainingMs.erase(Effects.Score2xRemainingMs.begin() + static_cast<std::ptrdiff_t>(Index));
				EmitEnded(EPowerupType::Score2x);
				continue;
			}
			++Index;
		}

		if (Effects.SlowMotionRemainingMs > 0.0)
		{
			Effects.SlowMotionRemainingMs -= RealDeltaMs;
			if (Effects.SlowMotionRemainingMs <= 0.0)
			{
				Effects.SlowMotionRemainingMs = 0.0;
				EmitEnded(EPowerupType::SlowMotion);
			}
		}

		if (Effects.ShieldRemainingMs > 0.0)
		{
			Effects.ShieldRemainingMs -= RealDeltaMs;
			if (Effects.ShieldRemainingMs <= 0.0)
			{
				Effects.ShieldRemainingMs = 0.0;
				EmitEnded(EPowerupType::Shield);
			}
		}

		if (Effects.FeverRemainingMs > 0.0)
		{
			Effects.FeverRemainingMs -= RealDeltaMs;
			if (Effects.FeverRemainingMs <= 0.0)
			{
				Effects.FeverRemainingMs = 0.0;
				Effects.FeverMultiplier = 1.0;
				EmitEnded(EPowerupType::Fever);
			}
		}

		if (Effects.LaneCleanerRemainingMs > 0.0)
		{
			Effects.LaneCleanerRemainingMs -= RealDeltaMs;
			if (Effects.LaneCleanerRemainingMs <= 0.0)
			{
				// The player did not choose in time: clear the most crowded lane for them.
				ClearLane(PickLaneCleanerTarget(), SongTimeMs);
			}
		}
	}

	void FSimulation::UpdatePowerups(double RealDeltaMs, double NowMs)
	{
		if (bPowerupSpawning && RealTimeMs >= NextPowerupAtMs)
		{
			if (NowMs >= 0.0 && NowMs < SongLengthMs - Rules.PowerupSpawnTailMs)
			{
				const size_t TypeIndex = Random.PickWeighted(Params.PowerupWeights.data(), Params.PowerupWeights.size());
				ForceSpawnPowerup(static_cast<EPowerupType>(TypeIndex), Random.RangeInt(0, NumLanes - 1));
			}
			ScheduleNextPowerup();
		}

		for (FFallingPowerup& Powerup : Powerups)
		{
			Powerup.AgeMs += RealDeltaMs;
		}

		CollectPowerupsAtShip();

		for (size_t Index = 0; Index < Powerups.size();)
		{
			if (Powerups[Index].AgeMs >= Rules.PowerupLifetimeMs)
			{
				FEvent Event;
				Event.Type = EEventType::PowerupDespawned;
				Event.Lane = Powerups[Index].Lane;
				Event.Powerup = Powerups[Index].Type;
				Event.PowerupId = Powerups[Index].Id;
				Event.SongTimeMs = NowMs;
				Emit(Event);
				Powerups.erase(Powerups.begin() + static_cast<std::ptrdiff_t>(Index));
				continue;
			}
			++Index;
		}
	}

	void FSimulation::CollectPowerupsAtShip()
	{
		for (size_t Index = 0; Index < Powerups.size();)
		{
			const FFallingPowerup& Powerup = Powerups[Index];
			if (Powerup.Lane == ShipLane && std::abs(GetPowerupY(Powerup) - Rules.ShipY) <= Rules.PowerupCollectBand)
			{
				CollectPowerup(Index);
				continue;
			}
			++Index;
		}
	}

	void FSimulation::CollectPowerup(size_t Index)
	{
		const FFallingPowerup Powerup = Powerups[Index];
		Powerups.erase(Powerups.begin() + static_cast<std::ptrdiff_t>(Index));

		Score += Rules.PowerupCollectPoints;
		++Stats.PowerupsCollected;

		FEvent Event;
		Event.Type = EEventType::PowerupCollected;
		Event.Lane = Powerup.Lane;
		Event.Powerup = Powerup.Type;
		Event.PowerupId = Powerup.Id;
		Event.Points = Rules.PowerupCollectPoints;
		Event.SongTimeMs = SongTimeMs;
		Emit(Event);

		ApplyPowerup(Powerup.Type);
	}

	void FSimulation::ScheduleNextPowerup()
	{
		const double Jitter = Params.PowerupIntervalJitterMs;
		NextPowerupAtMs = RealTimeMs + std::max(1000.0, Params.PowerupIntervalMs + Random.Range(-Jitter, Jitter));
	}

	void FSimulation::ClearLane(int32_t Lane, double AtMs)
	{
		Effects.LaneCleanerRemainingMs = 0.0;
		if (Lane < 0 || Lane >= NumLanes)
		{
			return;
		}

		FLaneState& State = Lanes[static_cast<size_t>(Lane)];
		int32_t Cleared = 0;
		for (size_t Slot = State.Cursor; Slot < State.NoteIndices.size(); ++Slot)
		{
			FNote& Note = Notes[static_cast<size_t>(State.NoteIndices[Slot])];
			if (Note.TimeMs > AtMs + Params.ApproachTimeMs)
			{
				break;
			}
			if (Note.IsPending())
			{
				Note.bCleared = true;
				Note.ResolvedAtMs = AtMs;
				--PendingNotes;
				++Cleared;
			}
		}
		Stats.NotesCleared += Cleared;

		FEvent Event;
		Event.Type = EEventType::LaneCleared;
		Event.Lane = Lane;
		Event.Powerup = EPowerupType::LaneCleaner;
		Event.Count = Cleared;
		Event.SongTimeMs = AtMs;
		Emit(Event);
	}

	int32_t FSimulation::CountPendingNotes(int32_t Lane, double FromMs, double ToMs) const
	{
		const FLaneState& State = Lanes[static_cast<size_t>(Lane)];
		int32_t Count = 0;
		for (size_t Slot = State.Cursor; Slot < State.NoteIndices.size(); ++Slot)
		{
			const FNote& Note = Notes[static_cast<size_t>(State.NoteIndices[Slot])];
			if (Note.TimeMs >= ToMs)
			{
				break;
			}
			if (Note.IsPending() && Note.TimeMs >= FromMs)
			{
				++Count;
			}
		}
		return Count;
	}

	int32_t FSimulation::PickLaneCleanerTarget() const
	{
		int32_t BestLane = ShipLane;
		int32_t BestCount = -1;
		for (int32_t Lane = 0; Lane < NumLanes; ++Lane)
		{
			if (Lanes[static_cast<size_t>(Lane)].bCaptured)
			{
				continue;
			}
			const int32_t Count = CountPendingNotes(Lane, SongTimeMs - Params.GoodWindowMs, SongTimeMs + Params.ApproachTimeMs);
			if (Count > BestCount || (Count == BestCount && Lane == ShipLane))
			{
				BestLane = Lane;
				BestCount = Count;
			}
		}
		return BestLane;
	}

	int32_t FSimulation::PickAutoCaptureLane() const
	{
		// The lane most in need: prefer lanes that still have music coming, then the most
		// consecutive misses, then the lowest combo.
		int32_t BestLane = -1;
		bool bBestHasNotes = false;
		int32_t BestMisses = 0;
		int32_t BestCombo = 0;
		int32_t BestUpcoming = 0;
		for (int32_t Lane = 0; Lane < NumLanes; ++Lane)
		{
			const FLaneState& State = Lanes[static_cast<size_t>(Lane)];
			if (State.bCaptured)
			{
				continue;
			}
			const int32_t Upcoming = CountPendingNotes(Lane, SongTimeMs - Params.GoodWindowMs, SongTimeMs + Rules.CaptureDurationMs);
			const bool bHasNotes = Upcoming > 0;

			bool bBetter = false;
			if (BestLane < 0)
			{
				bBetter = true;
			}
			else if (bHasNotes != bBestHasNotes)
			{
				bBetter = bHasNotes;
			}
			else if (State.ConsecutiveMisses != BestMisses)
			{
				bBetter = State.ConsecutiveMisses > BestMisses;
			}
			else if (State.Combo != BestCombo)
			{
				bBetter = State.Combo < BestCombo;
			}
			else
			{
				bBetter = Upcoming > BestUpcoming;
			}

			if (bBetter)
			{
				BestLane = Lane;
				bBestHasNotes = bHasNotes;
				BestMisses = State.ConsecutiveMisses;
				BestCombo = State.Combo;
				BestUpcoming = Upcoming;
			}
		}
		return BestLane;
	}

	void FSimulation::CheckCompletion(double NowMs)
	{
		if (IsFinished() || NowMs < SongLengthMs || PendingNotes > 0)
		{
			return;
		}
		bComplete = true;
		Score += Rules.SongCompleteBonus;

		FEvent Event;
		Event.Type = EEventType::SongComplete;
		Event.Points = Rules.SongCompleteBonus;
		Event.SongTimeMs = NowMs;
		Emit(Event);
	}
}
