#include "Data/AmplitudeSongLibrary.h"

#include "Algo/Sort.h"
#include "Amplitude.h"
#include "Core/AmpChart.h"
#include "Core/AmpMp3.h"
#include "Core/AmpRules.h"
#include "Core/AmpWav.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include <string>

namespace
{
	using FJsonArray = TArray<TSharedPtr<FJsonValue>>;

	const TSharedPtr<FJsonValue>* FindField(const FJsonObject& Object, const TCHAR* Field)
	{
		const TSharedPtr<FJsonValue>* Value = Object.Values.Find(Field);
		return (Value != nullptr && Value->IsValid() && (*Value)->Type != EJson::Null) ? Value : nullptr;
	}

	const FJsonObject* GetObjectField(const FJsonObject& Object, const TCHAR* Field)
	{
		const TSharedPtr<FJsonValue>* Value = FindField(Object, Field);
		return (Value != nullptr && (*Value)->Type == EJson::Object) ? (*Value)->AsObject().Get() : nullptr;
	}

	const FJsonArray* GetArrayField(const FJsonObject& Object, const TCHAR* Field)
	{
		const TSharedPtr<FJsonValue>* Value = FindField(Object, Field);
		return (Value != nullptr && (*Value)->Type == EJson::Array) ? &(*Value)->AsArray() : nullptr;
	}

	bool GetNumberField(const FJsonObject& Object, const TCHAR* Field, double& Out)
	{
		const TSharedPtr<FJsonValue>* Value = FindField(Object, Field);
		if (Value == nullptr)
		{
			return false;
		}
		if ((*Value)->Type == EJson::Number)
		{
			Out = (*Value)->AsNumber();
			return true;
		}
		if ((*Value)->Type == EJson::String && (*Value)->AsString().IsNumeric())
		{
			Out = FCString::Atod(*(*Value)->AsString());
			return true;
		}
		return false;
	}

	bool GetStringField(const FJsonObject& Object, const TCHAR* Field, FString& Out)
	{
		const TSharedPtr<FJsonValue>* Value = FindField(Object, Field);
		if (Value == nullptr || (*Value)->Type != EJson::String)
		{
			return false;
		}
		Out = (*Value)->AsString();
		return true;
	}

	int32 FindKeyIndex(const FString& Key, int32 (*Finder)(const char*))
	{
		const FString Lower = Key.ToLower().TrimStartAndEnd();
		return Finder(StringCast<ANSICHAR>(*Lower).Get());
	}

	/** Lanes are 1-based numbers (spec) or instrument names. */
	int32 ParseLaneValue(const FJsonValue& Value)
	{
		int32 Lane = INDEX_NONE;
		if (Value.Type == EJson::Number)
		{
			Lane = FMath::RoundToInt32(Value.AsNumber()) - 1;
		}
		else if (Value.Type == EJson::String)
		{
			const FString Text = Value.AsString();
			Lane = Text.IsNumeric() ? FCString::Atoi(*Text) - 1 : FindKeyIndex(Text, &Amp::FindLaneByInstrumentKey);
		}
		return (Lane >= 0 && Lane < Amp::NumLanes) ? Lane : INDEX_NONE;
	}

	/** Columns are 1-3 (left to right) or "left" / "middle" / "right". */
	int32 ParseColumnValue(const FJsonValue& Value)
	{
		int32 Column = INDEX_NONE;
		if (Value.Type == EJson::Number)
		{
			Column = FMath::RoundToInt32(Value.AsNumber()) - 1;
		}
		else if (Value.Type == EJson::String)
		{
			const FString Text = Value.AsString().ToLower().TrimStartAndEnd();
			if (Text.IsNumeric())
			{
				Column = FCString::Atoi(*Text) - 1;
			}
			else if (Text == TEXT("left") || Text == TEXT("l"))
			{
				Column = 0;
			}
			else if (Text == TEXT("middle") || Text == TEXT("center") || Text == TEXT("m"))
			{
				Column = 1;
			}
			else if (Text == TEXT("right") || Text == TEXT("r"))
			{
				Column = 2;
			}
		}
		return (Column >= 0 && Column < Amp::NumColumns) ? Column : INDEX_NONE;
	}

	/** A single column or an array of columns (a chord); false if any value is invalid. */
	bool ParseColumnMask(const FJsonValue& Value, uint8& OutMask)
	{
		OutMask = 0;
		if (Value.Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Element : Value.AsArray())
			{
				const int32 Column = Element.IsValid() ? ParseColumnValue(*Element) : INDEX_NONE;
				if (Column == INDEX_NONE)
				{
					return false;
				}
				OutMask = static_cast<uint8>(OutMask | (1u << Column));
			}
			return OutMask != 0;
		}
		const int32 Column = ParseColumnValue(Value);
		if (Column == INDEX_NONE)
		{
			return false;
		}
		OutMask = static_cast<uint8>(1u << Column);
		return true;
	}

	/** Gems of charts written without columns walk middle, left, middle, right through each lane. */
	uint8 DefaultColumnMask(int32 NoteInLane)
	{
		static constexpr int32 Pattern[] = {1, 0, 1, 2};
		return static_cast<uint8>(1u << Pattern[NoteInLane % UE_ARRAY_COUNT(Pattern)]);
	}

	/** The first N columns, used when an old multi-lane chord is folded into one lane. */
	uint8 LeadingColumnsMask(int32 Count)
	{
		return static_cast<uint8>((1u << FMath::Clamp(Count, 1, Amp::NumColumns)) - 1u);
	}

	void ParseChart(const FJsonArray& Array, const FString& ChartName, std::vector<Amp::FChartEntry>& Out, TArray<FString>& Warnings)
	{
		int32 NextAutoId = 1;
		int32 NotesPerLane[Amp::NumLanes] = {};
		for (int32 Index = 0; Index < Array.Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& Value = Array[Index];
			if (!Value.IsValid() || Value->Type != EJson::Object)
			{
				Warnings.Add(FString::Printf(TEXT("%s[%d]: note is not an object"), *ChartName, Index));
				continue;
			}
			const FJsonObject& Note = *Value->AsObject();

			double TimeMs = 0.0;
			if (!GetNumberField(Note, TEXT("time_ms"), TimeMs))
			{
				Warnings.Add(FString::Printf(TEXT("%s[%d]: missing time_ms"), *ChartName, Index));
				continue;
			}

			// "lane": one lane. An array of lanes (older charts) becomes a chord in the first lane listed.
			const TSharedPtr<FJsonValue>* LaneValue = FindField(Note, TEXT("lane"));
			if (LaneValue == nullptr)
			{
				LaneValue = FindField(Note, TEXT("lanes"));
			}
			int32 Lane = INDEX_NONE;
			int32 LegacyChordSize = 0;
			if (LaneValue != nullptr && (*LaneValue)->Type == EJson::Array)
			{
				for (const TSharedPtr<FJsonValue>& Element : (*LaneValue)->AsArray())
				{
					const int32 Parsed = Element.IsValid() ? ParseLaneValue(*Element) : INDEX_NONE;
					if (Parsed == INDEX_NONE)
					{
						Lane = INDEX_NONE;
						break;
					}
					Lane = LegacyChordSize == 0 ? Parsed : Lane;
					++LegacyChordSize;
				}
			}
			else if (LaneValue != nullptr)
			{
				Lane = ParseLaneValue(**LaneValue);
			}
			if (Lane == INDEX_NONE)
			{
				Warnings.Add(FString::Printf(TEXT("%s[%d]: lane must be 1-6 or an instrument name"), *ChartName, Index));
				continue;
			}

			uint8 Mask = 0;
			const TSharedPtr<FJsonValue>* ColumnValue = FindField(Note, TEXT("column"));
			if (ColumnValue == nullptr)
			{
				ColumnValue = FindField(Note, TEXT("columns"));
			}
			if (ColumnValue != nullptr)
			{
				if (!ParseColumnMask(**ColumnValue, Mask))
				{
					Warnings.Add(FString::Printf(TEXT("%s[%d]: column must be 1-3 (left, middle, right) or an array of them"), *ChartName, Index));
					continue;
				}
			}
			else if (LegacyChordSize > 1)
			{
				Mask = LeadingColumnsMask(LegacyChordSize);
			}
			else
			{
				Mask = DefaultColumnMask(NotesPerLane[Lane]);
			}
			++NotesPerLane[Lane];

			double IdValue = 0.0;
			const int32 Id = GetNumberField(Note, TEXT("id"), IdValue) ? static_cast<int32>(IdValue) : NextAutoId;
			NextAutoId = FMath::Max(NextAutoId, Id + 1);

			const int32 ColumnCount = Amp::CountColumns(Mask);
			Amp::ENoteType Type = Amp::NoteTypeForColumnCount(ColumnCount);
			FString TypeText;
			if (GetStringField(Note, TEXT("type"), TypeText))
			{
				const int32 TypeIndex = FindKeyIndex(TypeText, &Amp::FindNoteTypeByKey);
				if (TypeIndex == INDEX_NONE)
				{
					Warnings.Add(FString::Printf(TEXT("%s[%d]: unknown note type '%s'"), *ChartName, Index, *TypeText));
				}
				else
				{
					Type = static_cast<Amp::ENoteType>(TypeIndex);
					const int32 Expected = Type == Amp::ENoteType::Double ? 2 : (Type == Amp::ENoteType::Triple ? 3 : -1);
					if (Expected > 0 && Expected != ColumnCount)
					{
						Warnings.Add(FString::Printf(TEXT("%s[%d]: '%s' note uses %d column(s)"), *ChartName, Index, *TypeText, ColumnCount));
					}
				}
			}

			double Count = 0.0;
			if (Type == Amp::ENoteType::Stream && GetNumberField(Note, TEXT("count"), Count))
			{
				double IntervalMs = 125.0;
				GetNumberField(Note, TEXT("interval_ms"), IntervalMs);
				Amp::AppendStream(Out, Id, TimeMs, Lane, Mask, static_cast<int32>(Count), IntervalMs);
				continue;
			}

			Amp::FChartEntry Entry;
			Entry.Id = Id;
			Entry.TimeMs = TimeMs;
			Entry.Lane = Lane;
			Entry.ColumnMask = Mask;
			Entry.Type = Type;
			Out.push_back(Entry);
		}
	}

	void ApplyDifficultyOverrides(const FJsonObject& Object, Amp::FDifficultyParams& Params)
	{
		double Value = 0.0;
		if (GetNumberField(Object, TEXT("note_density_multiplier"), Value))
		{
			Params.NoteDensity = FMath::Clamp(Value, 0.05, 4.0);
		}
		if (GetNumberField(Object, TEXT("note_speed"), Value))
		{
			Params.NoteSpeed = FMath::Max(50.0, Value);
			Params.ApproachTimeMs = Amp::ApproachTimeFromSpeed(Params.NoteSpeed);
		}
		if (GetNumberField(Object, TEXT("approach_time_ms"), Value))
		{
			Params.ApproachTimeMs = FMath::Clamp(Value, 300.0, 10000.0);
		}
		if (GetNumberField(Object, TEXT("perfect_window_ms"), Value))
		{
			Params.PerfectWindowMs = FMath::Clamp(Value, 5.0, 1000.0);
		}
		const bool bGoodSet = GetNumberField(Object, TEXT("good_window_ms"), Value);
		if (bGoodSet)
		{
			Params.GoodWindowMs = FMath::Clamp(Value, 5.0, 1500.0);
		}
		if (GetNumberField(Object, TEXT("early_miss_window_ms"), Value))
		{
			Params.EarlyMissWindowMs = Value;
		}
		else if (bGoodSet)
		{
			Params.EarlyMissWindowMs = Params.GoodWindowMs + 100.0;
		}
		Params.GoodWindowMs = FMath::Max(Params.GoodWindowMs, Params.PerfectWindowMs);
		Params.EarlyMissWindowMs = FMath::Max(Params.EarlyMissWindowMs, Params.GoodWindowMs);

		if (GetNumberField(Object, TEXT("energy_on_perfect"), Value))
		{
			Params.EnergyOnPerfect = FMath::RoundToInt32(Value);
		}
		if (GetNumberField(Object, TEXT("energy_on_good"), Value))
		{
			Params.EnergyOnGood = FMath::RoundToInt32(Value);
		}
		if (GetNumberField(Object, TEXT("energy_on_miss"), Value))
		{
			// Accept both -3 and 3 as "a miss costs 3 energy".
			Params.EnergyOnMiss = -FMath::Abs(FMath::RoundToInt32(Value));
		}

		if (const FJsonArray* Thresholds = GetArrayField(Object, TEXT("combo_thresholds")))
		{
			for (int32 Tier = 0; Tier < Amp::NumComboTiers && Tier < Thresholds->Num(); ++Tier)
			{
				double Threshold = 0.0;
				if ((*Thresholds)[Tier].IsValid() && (*Thresholds)[Tier]->TryGetNumber(Threshold))
				{
					Params.ComboThresholds[static_cast<size_t>(Tier)] = FMath::RoundToInt32(Threshold);
				}
			}
		}
		if (const FJsonArray* Multipliers = GetArrayField(Object, TEXT("combo_multipliers")))
		{
			for (int32 Tier = 0; Tier < Amp::NumComboTiers && Tier < Multipliers->Num(); ++Tier)
			{
				double Multiplier = 0.0;
				if ((*Multipliers)[Tier].IsValid() && (*Multipliers)[Tier]->TryGetNumber(Multiplier))
				{
					Params.ComboMultipliers[static_cast<size_t>(Tier)] = FMath::Max(1.0, Multiplier);
				}
			}
		}

		if (GetNumberField(Object, TEXT("powerup_interval_ms"), Value))
		{
			Params.PowerupIntervalMs = FMath::Max(1000.0, Value);
			Params.PowerupIntervalJitterMs = Params.PowerupIntervalMs * 0.2;
		}
		if (GetNumberField(Object, TEXT("powerup_interval_jitter_ms"), Value))
		{
			Params.PowerupIntervalJitterMs = FMath::Max(0.0, Value);
		}
		if (const FJsonObject* Weights = GetObjectField(Object, TEXT("powerup_weights")))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Weights->Values)
			{
				const int32 Type = FindKeyIndex(Pair.Key, &Amp::FindPowerupByKey);
				double Weight = 0.0;
				if (Type != INDEX_NONE && Pair.Value.IsValid() && Pair.Value->TryGetNumber(Weight))
				{
					Params.PowerupWeights[static_cast<size_t>(Type)] = FMath::Max(0.0, Weight);
				}
			}
		}
		if (GetNumberField(Object, TEXT("extra_chord_ratio"), Value))
		{
			Params.ExtraChordRatio = FMath::Clamp(Value, 0.0, 1.0);
		}
	}

	void ApplyRuleOverrides(const FJsonObject& Object, Amp::FGameRules& Rules)
	{
		double Value = 0.0;
		if (GetNumberField(Object, TEXT("starting_energy"), Value))
		{
			Rules.StartingEnergy = FMath::Clamp(FMath::RoundToInt32(Value), 1, Rules.MaxEnergy);
		}
		if (GetNumberField(Object, TEXT("capture_streak"), Value))
		{
			Rules.CaptureStreak = FMath::Max(1, FMath::RoundToInt32(Value));
		}
		if (GetNumberField(Object, TEXT("capture_duration_ms"), Value))
		{
			Rules.CaptureDurationMs = FMath::Max(1000.0, Value);
		}
		if (GetNumberField(Object, TEXT("capture_energy_cost"), Value))
		{
			Rules.CaptureEnergyCost = FMath::Max(0, FMath::RoundToInt32(Value));
		}
		if (GetNumberField(Object, TEXT("mute_miss_streak"), Value))
		{
			Rules.MuteMissStreak = FMath::Max(1, FMath::RoundToInt32(Value));
		}
		if (GetNumberField(Object, TEXT("perfect_points"), Value))
		{
			Rules.PerfectPoints = FMath::Max(0, FMath::RoundToInt32(Value));
		}
		if (GetNumberField(Object, TEXT("good_points"), Value))
		{
			Rules.GoodPoints = FMath::Max(0, FMath::RoundToInt32(Value));
		}
		if (GetNumberField(Object, TEXT("powerup_collect_points"), Value))
		{
			Rules.PowerupCollectPoints = FMath::Max(0, FMath::RoundToInt32(Value));
		}
		if (GetNumberField(Object, TEXT("song_complete_bonus"), Value))
		{
			Rules.SongCompleteBonus = FMath::Max(0, FMath::RoundToInt32(Value));
		}
		if (GetNumberField(Object, TEXT("powerup_collect_window_ms"), Value))
		{
			Rules.PowerupCollectWindowMs = FMath::Clamp(Value, 50.0, 1000.0);
		}
		if (GetNumberField(Object, TEXT("idle_lane_gain"), Value))
		{
			Rules.IdleLaneGain = FMath::Clamp(static_cast<float>(Value), 0.0f, 1.0f);
		}
		if (const TSharedPtr<FJsonValue>* AutoAdvance = FindField(Object, TEXT("auto_advance_on_capture")))
		{
			if ((*AutoAdvance)->Type == EJson::Boolean)
			{
				Rules.bAutoAdvanceOnCapture = (*AutoAdvance)->AsBool();
			}
		}
	}

	/** Relative paths are tried against the song folder, the songs root and the project; a bare file name in the song folder is the last resort. */
	FString ResolveSongPath(const FString& SongDirectory, const FString& Path)
	{
		if (Path.IsEmpty())
		{
			return FString();
		}
		if (!FPaths::IsRelative(Path))
		{
			return Path;
		}

		const FString Candidates[] = {
			SongDirectory / Path,
			FPaths::GetPath(SongDirectory) / Path,
			FPaths::ProjectDir() / Path,
			SongDirectory / FPaths::GetCleanFilename(Path)};
		for (const FString& Candidate : Candidates)
		{
			if (FPaths::FileExists(Candidate))
			{
				return FPaths::ConvertRelativePathToFull(Candidate);
			}
		}
		return FPaths::ConvertRelativePathToFull(Candidates[0]);
	}

	FString ToFString(const std::string& Text)
	{
		return FString(UTF8_TO_TCHAR(Text.c_str()));
	}

	bool IsWavPath(const FString& Path)
	{
		return FPaths::GetExtension(Path).Equals(TEXT("wav"), ESearchCase::IgnoreCase);
	}

	bool IsMp3Path(const FString& Path)
	{
		return FPaths::GetExtension(Path).Equals(TEXT("mp3"), ESearchCase::IgnoreCase);
	}
}

bool FAmplitudeSongDefinition::HasAudioConfigured() const
{
	if (!AudioPath.IsEmpty())
	{
		return true;
	}
	for (const FString& Stem : StemPaths)
	{
		if (!Stem.IsEmpty())
		{
			return true;
		}
	}
	return false;
}

int32 FAmplitudeSongDefinition::GetStarRating() const
{
	if (Rating >= 1 && Rating <= 5)
	{
		return Rating;
	}
	const double Seconds = FMath::Max(1.0, GetDisplayDurationMs() / 1000.0);
	const double NotesPerSecond = static_cast<double>(CountNotes(Amp::EDifficulty::Normal)) / Seconds;
	return FMath::Clamp(1 + FMath::FloorToInt32(NotesPerSecond / 1.2), 1, 5);
}

double FAmplitudeSongDefinition::GetFirstBeatMs() const
{
	return BeatMarkers.Num() > 0 ? BeatMarkers[0].TimeMs : 0.0;
}

std::vector<Amp::FNote> FAmplitudeSongDefinition::BuildNotes(Amp::EDifficulty Difficulty) const
{
	const int32 Index = static_cast<int32>(Difficulty);
	Amp::FChartBuildOptions Options;
	if (bHasDifficultyChart[Index])
	{
		return Amp::BuildNotes(DifficultyCharts[Index], Options);
	}

	const std::vector<Amp::FChartEntry>* Base = nullptr;
	double BaseDensity = 1.0;
	if (bHasBaseChart)
	{
		// The generic "notes" chart is authored at Normal (100%) density.
		Base = &BaseChart;
	}
	else
	{
		// Derive from the nearest authored chart, preferring Normal.
		const int32 Preference[] = {1, 2, 0, 3};
		for (const int32 Candidate : Preference)
		{
			if (bHasDifficultyChart[Candidate])
			{
				Base = &DifficultyCharts[Candidate];
				BaseDensity = DifficultyParams[Candidate].NoteDensity;
				break;
			}
		}
	}
	if (Base == nullptr)
	{
		return {};
	}

	Options.DensityScale = DifficultyParams[Index].NoteDensity / FMath::Max(0.01, BaseDensity);
	Options.ExtraChordRatio = DifficultyParams[Index].ExtraChordRatio;
	return Amp::BuildNotes(*Base, Options);
}

double FAmplitudeSongDefinition::GetDisplayDurationMs() const
{
	if (DurationMs > 0.0)
	{
		return DurationMs;
	}
	double LastNoteMs = 0.0;
	for (const Amp::FChartEntry& Entry : BaseChart)
	{
		LastNoteMs = FMath::Max(LastNoteMs, Entry.TimeMs);
	}
	for (const std::vector<Amp::FChartEntry>& Chart : DifficultyCharts)
	{
		for (const Amp::FChartEntry& Entry : Chart)
		{
			LastNoteMs = FMath::Max(LastNoteMs, Entry.TimeMs);
		}
	}
	return LastNoteMs + 2000.0;
}

TSharedPtr<const FAmplitudeSongDefinition> FAmplitudeSongLibrary::GetSong(int32 Index) const
{
	return Songs.IsValidIndex(Index) ? Songs[Index] : nullptr;
}

int32 FAmplitudeSongLibrary::IndexOfId(const FString& Id) const
{
	return Songs.IndexOfByPredicate([&Id](const TSharedPtr<const FAmplitudeSongDefinition>& Song) { return Song->Id == Id; });
}

FString FAmplitudeSongLibrary::GetDefaultSongDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Songs"));
}

void FAmplitudeSongLibrary::Scan(const TArray<FString>& AdditionalDirectories)
{
	Songs.Reset();
	Errors.Reset();
	ScannedDirectories.Reset();

	ScannedDirectories.Add(GetDefaultSongDirectory());
	for (const FString& Directory : AdditionalDirectories)
	{
		if (!Directory.TrimStartAndEnd().IsEmpty())
		{
			ScannedDirectories.AddUnique(FPaths::ConvertRelativePathToFull(Directory.TrimStartAndEnd()));
		}
	}

	IFileManager& FileManager = IFileManager::Get();
	for (const FString& Root : ScannedDirectories)
	{
		if (!FileManager.DirectoryExists(*Root))
		{
			continue;
		}

		TArray<FString> Folders;
		FileManager.FindFiles(Folders, *(Root / TEXT("*")), false, true);
		Folders.Sort();
		for (const FString& Folder : Folders)
		{
			const FString Directory = Root / Folder;
			FString JsonPath = Directory / TEXT("song.json");
			if (!FileManager.FileExists(*JsonPath))
			{
				TArray<FString> JsonFiles;
				FileManager.FindFiles(JsonFiles, *(Directory / TEXT("*.json")), true, false);
				if (JsonFiles.Num() == 0)
				{
					continue;
				}
				JsonFiles.Sort();
				JsonPath = Directory / JsonFiles[0];
			}

			FString JsonText;
			if (!FFileHelper::LoadFileToString(JsonText, *JsonPath))
			{
				Errors.Add(FString::Printf(TEXT("%s: could not be read"), *JsonPath));
				continue;
			}

			TSharedPtr<FAmplitudeSongDefinition> Song = MakeShared<FAmplitudeSongDefinition>();
			Song->Id = Folder;
			Song->JsonPath = JsonPath;
			FString Error;
			if (!ParseSongJson(JsonText, Directory, *Song, Error))
			{
				Errors.Add(FString::Printf(TEXT("%s: %s"), *JsonPath, *Error));
				UE_LOG(LogAmplitude, Warning, TEXT("Skipping song %s: %s"), *JsonPath, *Error);
				continue;
			}
			for (const FString& Warning : Song->Warnings)
			{
				UE_LOG(LogAmplitude, Warning, TEXT("%s: %s"), *JsonPath, *Warning);
			}
			if (IndexOfId(Song->Id) != INDEX_NONE)
			{
				Errors.Add(FString::Printf(TEXT("%s: a song folder named '%s' was already loaded"), *JsonPath, *Song->Id));
				continue;
			}
			Songs.Add(Song);
		}
	}

	Algo::Sort(Songs, [](const TSharedPtr<const FAmplitudeSongDefinition>& A, const TSharedPtr<const FAmplitudeSongDefinition>& B)
	{
		return A->Title < B->Title;
	});
	UE_LOG(LogAmplitude, Log, TEXT("Song library: %d song(s), %d error(s)"), Songs.Num(), Errors.Num());
}

bool FAmplitudeSongLibrary::ParseSongJson(const FString& JsonText, const FString& SongDirectory, FAmplitudeSongDefinition& OutSong, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = FString::Printf(TEXT("invalid JSON (%s)"), *Reader->GetErrorMessage());
		return false;
	}

	OutSong.Directory = SongDirectory;
	OutSong.Title = OutSong.Id;
	OutSong.LaneChannels.Init(INDEX_NONE, Amp::NumLanes);
	OutSong.StemPaths.Init(FString(), Amp::NumLanes);

	double Number = 0.0;
	if (const FJsonObject* Metadata = GetObjectField(*Root, TEXT("metadata")))
	{
		GetStringField(*Metadata, TEXT("title"), OutSong.Title);
		GetStringField(*Metadata, TEXT("artist"), OutSong.Artist);
		GetStringField(*Metadata, TEXT("album"), OutSong.Album);
		GetStringField(*Metadata, TEXT("version"), OutSong.Version);
		if (GetNumberField(*Metadata, TEXT("year"), Number))
		{
			OutSong.Year = static_cast<int32>(Number);
		}
		if (GetNumberField(*Metadata, TEXT("duration_ms"), Number))
		{
			OutSong.DurationMs = FMath::Max(0.0, Number);
		}
		if (GetNumberField(*Metadata, TEXT("bpm"), Number) && Number > 0.0)
		{
			OutSong.Bpm = Number;
		}
		if (GetNumberField(*Metadata, TEXT("rating"), Number))
		{
			OutSong.Rating = FMath::Clamp(FMath::RoundToInt32(Number), 0, 5);
		}
		if (GetNumberField(*Metadata, TEXT("offset_ms"), Number))
		{
			OutSong.OffsetMs = FMath::Clamp(Number, -500.0, 500.0);
		}
	}

	if (const FJsonObject* Audio = GetObjectField(*Root, TEXT("audio")))
	{
		FString FilePath;
		if (GetStringField(*Audio, TEXT("file_path"), FilePath))
		{
			OutSong.AudioPath = ResolveSongPath(SongDirectory, FilePath);
			// Without an explicit channel map, channels 0-5 follow the lane order.
			for (int32 Lane = 0; Lane < Amp::NumLanes; ++Lane)
			{
				OutSong.LaneChannels[Lane] = Lane;
			}
		}
		if (const FJsonObject* Channels = GetObjectField(*Audio, TEXT("channels")))
		{
			OutSong.LaneChannels.Init(INDEX_NONE, Amp::NumLanes);
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Channels->Values)
			{
				double Channel = 0.0;
				if (!Pair.Value.IsValid() || !Pair.Value->TryGetNumber(Channel))
				{
					continue;
				}
				const int32 Lane = FindKeyIndex(Pair.Key, &Amp::FindLaneByInstrumentKey);
				if (Lane != INDEX_NONE)
				{
					OutSong.LaneChannels[Lane] = static_cast<int32>(Channel);
				}
				else if (Pair.Key.Equals(TEXT("master"), ESearchCase::IgnoreCase))
				{
					OutSong.MasterChannel = static_cast<int32>(Channel);
				}
				else
				{
					OutSong.Warnings.Add(FString::Printf(TEXT("audio.channels: unknown instrument '%s'"), *Pair.Key));
				}
			}
		}
		if (const FJsonObject* Stems = GetObjectField(*Audio, TEXT("stems")))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Stems->Values)
			{
				const int32 Lane = FindKeyIndex(Pair.Key, &Amp::FindLaneByInstrumentKey);
				if (Lane != INDEX_NONE && Pair.Value.IsValid() && Pair.Value->Type == EJson::String)
				{
					OutSong.StemPaths[Lane] = ResolveSongPath(SongDirectory, Pair.Value->AsString());
				}
			}
		}
		if (GetNumberField(*Audio, TEXT("offset_ms"), Number))
		{
			OutSong.OffsetMs = FMath::Clamp(Number, -500.0, 500.0);
		}
	}

	const FJsonObject* Difficulties = GetObjectField(*Root, TEXT("difficulties"));
	for (int32 Index = 0; Index < Amp::NumDifficulties; ++Index)
	{
		const Amp::EDifficulty Difficulty = static_cast<Amp::EDifficulty>(Index);
		OutSong.DifficultyParams[Index] = Amp::GetDefaultDifficultyParams(Difficulty);
		if (Difficulties != nullptr)
		{
			if (const FJsonObject* Overrides = GetObjectField(*Difficulties, ANSI_TO_TCHAR(Amp::GetDifficultyKey(Difficulty))))
			{
				ApplyDifficultyOverrides(*Overrides, OutSong.DifficultyParams[Index]);
			}
		}
	}

	if (const FJsonObject* RuleOverrides = GetObjectField(*Root, TEXT("rules")))
	{
		ApplyRuleOverrides(*RuleOverrides, OutSong.Rules);
	}

	if (const FJsonArray* Notes = GetArrayField(*Root, TEXT("notes")))
	{
		ParseChart(*Notes, TEXT("notes"), OutSong.BaseChart, OutSong.Warnings);
		OutSong.bHasBaseChart = !OutSong.BaseChart.empty();
	}
	for (int32 Index = 0; Index < Amp::NumDifficulties; ++Index)
	{
		const FString Field = FString(TEXT("notes_")) + ANSI_TO_TCHAR(Amp::GetDifficultyKey(static_cast<Amp::EDifficulty>(Index)));
		if (const FJsonArray* Notes = GetArrayField(*Root, *Field))
		{
			ParseChart(*Notes, Field, OutSong.DifficultyCharts[Index], OutSong.Warnings);
			OutSong.bHasDifficultyChart[Index] = !OutSong.DifficultyCharts[Index].empty();
		}
	}

	bool bHasAnyChart = OutSong.bHasBaseChart;
	for (const bool bHasChart : OutSong.bHasDifficultyChart)
	{
		bHasAnyChart = bHasAnyChart || bHasChart;
	}
	if (!bHasAnyChart)
	{
		OutError = TEXT("the song has no notes (expected \"notes\" or \"notes_<difficulty>\" arrays)");
		return false;
	}

	if (const FJsonObject* Events = GetObjectField(*Root, TEXT("events")))
	{
		if (const FJsonArray* Markers = GetArrayField(*Events, TEXT("beat_markers")))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Markers)
			{
				if (!Value.IsValid() || Value->Type != EJson::Object)
				{
					continue;
				}
				FAmplitudeBeatMarker Marker;
				if (GetNumberField(*Value->AsObject(), TEXT("time_ms"), Marker.TimeMs))
				{
					double Beat = 0.0;
					GetNumberField(*Value->AsObject(), TEXT("beat"), Beat);
					Marker.Beat = static_cast<int32>(Beat);
					OutSong.BeatMarkers.Add(Marker);
				}
			}
			OutSong.BeatMarkers.Sort([](const FAmplitudeBeatMarker& A, const FAmplitudeBeatMarker& B) { return A.TimeMs < B.TimeMs; });
		}
	}

	for (int32 Index = 0; Index < Amp::NumDifficulties; ++Index)
	{
		OutSong.NoteCounts[Index] = static_cast<int32>(OutSong.BuildNotes(static_cast<Amp::EDifficulty>(Index)).size());
	}

	if (!OutSong.AudioPath.IsEmpty() && !IsWavPath(OutSong.AudioPath))
	{
		OutSong.Warnings.Add(FString::Printf(TEXT("%s: only WAV audio can be streamed per instrument at runtime; convert it to a multichannel WAV"), *OutSong.AudioPath));
	}
	return true;
}

FAmplitudeAudioLoadResult FAmplitudeSongLibrary::LoadAudio(const FAmplitudeSongDefinition& Song)
{
	FAmplitudeAudioLoadResult Result;
	std::shared_ptr<Amp::FSongAudio> Audio = std::make_shared<Amp::FSongAudio>();
	int32 SampleRate = 0;
	TArray<bool> LaneLoaded;
	LaneLoaded.Init(false, Amp::NumLanes);

	auto AcceptRate = [&Result, &SampleRate](int32 Rate, const FString& Path)
	{
		if (SampleRate == 0)
		{
			SampleRate = Rate;
			return true;
		}
		if (Rate != SampleRate)
		{
			Result.Warnings.Add(FString::Printf(TEXT("%s is %d Hz but the song uses %d Hz; track skipped"), *Path, Rate, SampleRate));
			return false;
		}
		return true;
	};

	// Per-instrument stems first.
	for (int32 Lane = 0; Lane < Amp::NumLanes; ++Lane)
	{
		const FString& Path = Song.StemPaths.IsValidIndex(Lane) ? Song.StemPaths[Lane] : FString();
		if (Path.IsEmpty())
		{
			continue;
		}
		if (!IsWavPath(Path) && !IsMp3Path(Path))
		{
			Result.Warnings.Add(FString::Printf(TEXT("%s: unsupported format (stems must be WAV or MP3)"), *Path));
			continue;
		}
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path))
		{
			Result.Warnings.Add(FString::Printf(TEXT("%s: file not found"), *Path));
			continue;
		}
		Amp::FWavInfo Info;
		Amp::FPcmTrack Track;
		std::string Error;
		const bool bDecoded = IsMp3Path(Path)
			? Amp::DecodeMp3Stem(Bytes.GetData(), static_cast<size_t>(Bytes.Num()), Info, Track, Error)
			: Amp::DecodeWavStem(Bytes.GetData(), static_cast<size_t>(Bytes.Num()), Info, Track, Error);
		if (!bDecoded)
		{
			Result.Warnings.Add(FString::Printf(TEXT("%s: %s"), *Path, *ToFString(Error)));
			continue;
		}
		if (AcceptRate(Info.SampleRate, Path))
		{
			Audio->Lanes[static_cast<size_t>(Lane)] = MoveTemp(Track);
			LaneLoaded[Lane] = true;
		}
	}

	// Then the multitrack file for the lanes that do not have a stem.
	if (!Song.AudioPath.IsEmpty())
	{
		std::vector<int32_t> Channels;
		bool bNeedsFile = false;
		for (int32 Lane = 0; Lane < Amp::NumLanes; ++Lane)
		{
			const int32 Channel = LaneLoaded[Lane] ? INDEX_NONE : Song.LaneChannels[Lane];
			Channels.push_back(Channel);
			bNeedsFile = bNeedsFile || Channel != INDEX_NONE;
		}

		TArray<uint8> Bytes;
		if (!bNeedsFile)
		{
			// Every lane already has a stem.
		}
		else if (!IsWavPath(Song.AudioPath))
		{
			Result.Error = FString::Printf(TEXT("%s: unsupported format. Export the song as a multichannel WAV (see README)."), *FPaths::GetCleanFilename(Song.AudioPath));
		}
		else if (!FFileHelper::LoadFileToArray(Bytes, *Song.AudioPath))
		{
			Result.Error = FString::Printf(TEXT("Audio file not found: %s"), *Song.AudioPath);
		}
		else
		{
			Amp::FWavInfo Info;
			std::vector<Amp::FPcmTrack> Tracks;
			std::string Error;
			if (!Amp::DecodeWavChannels(Bytes.GetData(), static_cast<size_t>(Bytes.Num()), Channels, Info, Tracks, Error))
			{
				Result.Error = FString::Printf(TEXT("%s: %s"), *FPaths::GetCleanFilename(Song.AudioPath), *ToFString(Error));
			}
			else if (AcceptRate(Info.SampleRate, Song.AudioPath))
			{
				for (int32 Lane = 0; Lane < Amp::NumLanes; ++Lane)
				{
					if (Channels[static_cast<size_t>(Lane)] == INDEX_NONE)
					{
						continue;
					}
					if (Tracks[static_cast<size_t>(Lane)].IsEmpty())
					{
						Result.Warnings.Add(FString::Printf(TEXT("%s has no channel %d for %s"), *FPaths::GetCleanFilename(Song.AudioPath),
							Channels[static_cast<size_t>(Lane)], ANSI_TO_TCHAR(Amp::GetLaneInstrumentName(Lane))));
						continue;
					}
					Audio->Lanes[static_cast<size_t>(Lane)] = MoveTemp(Tracks[static_cast<size_t>(Lane)]);
				}
			}
		}
	}

	Audio->SampleRate = SampleRate > 0 ? SampleRate : 44100;
	if (Audio->HasAnyAudio())
	{
		Result.Audio = Audio;
	}
	else if (Result.Error.IsEmpty() && Song.HasAudioConfigured())
	{
		Result.Error = Result.Warnings.Num() > 0 ? Result.Warnings[0] : FString(TEXT("No playable audio tracks were found."));
	}
	return Result;
}
