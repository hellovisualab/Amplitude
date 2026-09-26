#include "Data/AmplitudeLeaderboard.h"

#include "Amplitude.h"
#include "Core/AmpRules.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	constexpr int32 FileVersion = 1;
}

FString FAmplitudeLeaderboard::GetFilePath()
{
	return FPaths::ProjectSavedDir() / TEXT("Amplitude") / TEXT("Leaderboards.json");
}

FString FAmplitudeLeaderboard::MakeKey(const FString& SongId, Amp::EDifficulty Difficulty)
{
	return SongId + TEXT("|") + ANSI_TO_TCHAR(Amp::GetDifficultyKey(Difficulty));
}

void FAmplitudeLeaderboard::Load()
{
	Tables.Reset();

	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *GetFilePath()))
	{
		return;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogAmplitude, Warning, TEXT("Leaderboard file %s is corrupt and was ignored"), *GetFilePath());
		return;
	}

	const TSharedPtr<FJsonObject>* TablesObject = nullptr;
	if (!Root->TryGetObjectField(TEXT("tables"), TablesObject) || TablesObject == nullptr)
	{
		return;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*TablesObject)->Values)
	{
		if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Array)
		{
			continue;
		}
		TArray<FAmplitudeScoreEntry>& Entries = Tables.Add(Pair.Key);
		for (const TSharedPtr<FJsonValue>& Value : Pair.Value->AsArray())
		{
			if (!Value.IsValid() || Value->Type != EJson::Object)
			{
				continue;
			}
			const TSharedPtr<FJsonObject>& Object = Value->AsObject();
			FAmplitudeScoreEntry Entry;
			double Number = 0.0;
			Object->TryGetStringField(TEXT("name"), Entry.PlayerName);
			if (Object->TryGetNumberField(TEXT("score"), Number))
			{
				Entry.Score = static_cast<int64>(Number);
			}
			Object->TryGetNumberField(TEXT("accuracy"), Entry.AccuracyPercent);
			Object->TryGetNumberField(TEXT("perfect"), Entry.Perfect);
			Object->TryGetNumberField(TEXT("good"), Entry.Good);
			Object->TryGetNumberField(TEXT("miss"), Entry.Miss);
			Object->TryGetBoolField(TEXT("completed"), Entry.bCompleted);
			FString Date;
			if (Object->TryGetStringField(TEXT("date"), Date))
			{
				FDateTime::ParseIso8601(*Date, Entry.Date);
			}
			Entries.Add(Entry);
		}
		Entries.Sort([](const FAmplitudeScoreEntry& A, const FAmplitudeScoreEntry& B) { return A.Score > B.Score; });
		if (Entries.Num() > MaxEntries)
		{
			Entries.SetNum(MaxEntries);
		}
	}
}

bool FAmplitudeLeaderboard::Save() const
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), FileVersion);

	const TSharedRef<FJsonObject> TablesObject = MakeShared<FJsonObject>();
	for (const TPair<FString, TArray<FAmplitudeScoreEntry>>& Pair : Tables)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FAmplitudeScoreEntry& Entry : Pair.Value)
		{
			const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("name"), Entry.PlayerName);
			Object->SetNumberField(TEXT("score"), static_cast<double>(Entry.Score));
			Object->SetNumberField(TEXT("accuracy"), Entry.AccuracyPercent);
			Object->SetNumberField(TEXT("perfect"), Entry.Perfect);
			Object->SetNumberField(TEXT("good"), Entry.Good);
			Object->SetNumberField(TEXT("miss"), Entry.Miss);
			Object->SetBoolField(TEXT("completed"), Entry.bCompleted);
			Object->SetStringField(TEXT("date"), Entry.Date.ToIso8601());
			Values.Add(MakeShared<FJsonValueObject>(Object));
		}
		TablesObject->SetArrayField(Pair.Key, Values);
	}
	Root->SetObjectField(TEXT("tables"), TablesObject);

	FString Text;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		return false;
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(GetFilePath()), true);
	if (!FFileHelper::SaveStringToFile(Text, *GetFilePath()))
	{
		UE_LOG(LogAmplitude, Warning, TEXT("Could not write leaderboard file %s"), *GetFilePath());
		return false;
	}
	return true;
}

const TArray<FAmplitudeScoreEntry>& FAmplitudeLeaderboard::GetEntries(const FString& SongId, Amp::EDifficulty Difficulty) const
{
	static const TArray<FAmplitudeScoreEntry> Empty;
	const TArray<FAmplitudeScoreEntry>* Entries = Tables.Find(MakeKey(SongId, Difficulty));
	return Entries != nullptr ? *Entries : Empty;
}

const FAmplitudeScoreEntry* FAmplitudeLeaderboard::GetBest(const FString& SongId, Amp::EDifficulty Difficulty) const
{
	const TArray<FAmplitudeScoreEntry>& Entries = GetEntries(SongId, Difficulty);
	return Entries.Num() > 0 ? &Entries[0] : nullptr;
}

int32 FAmplitudeLeaderboard::Submit(const FString& SongId, Amp::EDifficulty Difficulty, const FAmplitudeScoreEntry& Entry)
{
	TArray<FAmplitudeScoreEntry>& Entries = Tables.FindOrAdd(MakeKey(SongId, Difficulty));

	// Ties keep the earlier score ahead.
	int32 Rank = 0;
	while (Rank < Entries.Num() && Entries[Rank].Score >= Entry.Score)
	{
		++Rank;
	}
	if (Rank >= MaxEntries)
	{
		return INDEX_NONE;
	}

	Entries.Insert(Entry, Rank);
	if (Entries.Num() > MaxEntries)
	{
		Entries.SetNum(MaxEntries);
	}
	Save();
	return Rank;
}
