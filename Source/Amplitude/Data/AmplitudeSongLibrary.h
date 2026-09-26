#pragma once

#include "CoreMinimal.h"
#include "Core/AmpStemMixer.h"
#include "Core/AmpTypes.h"

#include <array>
#include <memory>
#include <vector>

struct FAmplitudeBeatMarker
{
	double TimeMs = 0.0;
	int32 Beat = 0;
};

/** Everything parsed from a song folder's JSON file (spec sections 14.1 and 20). Immutable once loaded. */
struct FAmplitudeSongDefinition
{
	/** Folder name; used as the leaderboard key. */
	FString Id;
	FString Directory;
	FString JsonPath;

	FString Title;
	FString Artist;
	FString Album;
	FString Version;
	int32 Year = 0;
	double DurationMs = 0.0;
	double Bpm = 120.0;
	/** 1-5 stars from metadata.rating, 0 when the song does not specify one. */
	int32 Rating = 0;
	/** Per-song audio offset (spec 10.2.2), added to the player's global offset. */
	double OffsetMs = 0.0;

	/** Multitrack file (absolute path) and the channel each lane instrument lives on (-1 = none). */
	FString AudioPath;
	TArray<int32> LaneChannels;
	int32 MasterChannel = INDEX_NONE;
	/** Optional per-instrument stem files (absolute paths); they take precedence over AudioPath channels. */
	TArray<FString> StemPaths;

	/** "notes": the base chart, scaled per difficulty by note density. */
	std::vector<Amp::FChartEntry> BaseChart;
	bool bHasBaseChart = false;
	/** "notes_<difficulty>": hand-authored charts used exactly as written. */
	std::array<std::vector<Amp::FChartEntry>, Amp::NumDifficulties> DifficultyCharts;
	std::array<bool, Amp::NumDifficulties> bHasDifficultyChart{};
	std::array<Amp::FDifficultyParams, Amp::NumDifficulties> DifficultyParams;
	/** Core rules (capture, mute, energy, points), optionally overridden by the song's "rules" object. */
	Amp::FGameRules Rules;

	TArray<FAmplitudeBeatMarker> BeatMarkers;
	TArray<FString> Warnings;
	/** Note count per difficulty, cached when the song is parsed. */
	std::array<int32, Amp::NumDifficulties> NoteCounts{};

	bool HasAudioConfigured() const;
	/** Rating from metadata, otherwise estimated from the Normal chart's notes per second. */
	int32 GetStarRating() const;
	double GetFirstBeatMs() const;
	/** Chart for a difficulty: the authored one, or the base chart scaled by the difficulty's note density. */
	std::vector<Amp::FNote> BuildNotes(Amp::EDifficulty Difficulty) const;
	int32 CountNotes(Amp::EDifficulty Difficulty) const { return NoteCounts[static_cast<size_t>(Difficulty)]; }
	double GetDisplayDurationMs() const;
};

struct FAmplitudeAudioLoadResult
{
	std::shared_ptr<Amp::FSongAudio> Audio;
	FString Error;
	TArray<FString> Warnings;
};

/** Finds and parses songs in <Project>/Songs (plus any extra folders from the user settings). */
class AMPLITUDE_API FAmplitudeSongLibrary
{
public:
	void Scan(const TArray<FString>& AdditionalDirectories);

	const TArray<TSharedPtr<const FAmplitudeSongDefinition>>& GetSongs() const { return Songs; }
	int32 Num() const { return Songs.Num(); }
	TSharedPtr<const FAmplitudeSongDefinition> GetSong(int32 Index) const;
	int32 IndexOfId(const FString& Id) const;
	const TArray<FString>& GetScannedDirectories() const { return ScannedDirectories; }
	const TArray<FString>& GetErrors() const { return Errors; }

	static FString GetDefaultSongDirectory();
	static bool ParseSongJson(const FString& JsonText, const FString& SongDirectory, FAmplitudeSongDefinition& OutSong, FString& OutError);

	/** Decodes the song's audio. Blocking: call it from a worker thread. */
	static FAmplitudeAudioLoadResult LoadAudio(const FAmplitudeSongDefinition& Song);

private:
	TArray<TSharedPtr<const FAmplitudeSongDefinition>> Songs;
	TArray<FString> ScannedDirectories;
	TArray<FString> Errors;
};
