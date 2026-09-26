#pragma once

#include "CoreMinimal.h"
#include "Core/AmpTypes.h"

struct FAmplitudeScoreEntry
{
	FString PlayerName;
	int64 Score = 0;
	double AccuracyPercent = 0.0;
	int32 Perfect = 0;
	int32 Good = 0;
	int32 Miss = 0;
	bool bCompleted = false;
	FDateTime Date;
};

/** Local top-10 table per song and difficulty (spec 9.4), stored as JSON under Saved/Amplitude. */
class AMPLITUDE_API FAmplitudeLeaderboard
{
public:
	static constexpr int32 MaxEntries = 10;

	void Load();
	bool Save() const;
	static FString GetFilePath();

	const TArray<FAmplitudeScoreEntry>& GetEntries(const FString& SongId, Amp::EDifficulty Difficulty) const;
	const FAmplitudeScoreEntry* GetBest(const FString& SongId, Amp::EDifficulty Difficulty) const;

	/** Inserts the score and saves. Returns its 0-based rank, or INDEX_NONE if it did not make the table. */
	int32 Submit(const FString& SongId, Amp::EDifficulty Difficulty, const FAmplitudeScoreEntry& Entry);

private:
	static FString MakeKey(const FString& SongId, Amp::EDifficulty Difficulty);

	TMap<FString, TArray<FAmplitudeScoreEntry>> Tables;
};
