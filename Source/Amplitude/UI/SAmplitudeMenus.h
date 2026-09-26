#pragma once

#include "CoreMinimal.h"
#include "Core/AmpTypes.h"
#include "UI/SAmplitudeWidgets.h"

class AAmplitudeDirector;
class SAmplitudeButton;

/** Spec 11.3.1. */
class SAmplitudeMainMenu : public SAmplitudeScreen
{
public:
	SLATE_BEGIN_ARGS(SAmplitudeMainMenu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AAmplitudeDirector>, Director)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};

/** Spec 11.3.2: song list, song details, difficulty choice and personal best. */
class SAmplitudeSongSelect : public SAmplitudeScreen
{
public:
	SLATE_BEGIN_ARGS(SAmplitudeSongSelect) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AAmplitudeDirector>, Director)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

protected:
	virtual void OnBack() override;

private:
	TSharedRef<SWidget> MakeEmptyLibraryContent();
	TSharedRef<SWidget> MakeDetails();
	FText GetDetailText(int32 Line) const;

	TSharedPtr<SAmplitudeButton> PlayButton;
};

/** Spec 11.3.3. */
class SAmplitudePauseMenu : public SAmplitudeScreen
{
public:
	SLATE_BEGIN_ARGS(SAmplitudePauseMenu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AAmplitudeDirector>, Director)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

protected:
	virtual void OnBack() override;
};

/** Spec 11.3.4 (game over) and 11.3.5 (song complete). */
class SAmplitudeResultsScreen : public SAmplitudeScreen
{
public:
	SLATE_BEGIN_ARGS(SAmplitudeResultsScreen) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AAmplitudeDirector>, Director)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

protected:
	virtual void OnBack() override;
};

/** Spec 11.3.6: top 10 per song and difficulty. */
class SAmplitudeLeaderboardScreen : public SAmplitudeScreen
{
public:
	SLATE_BEGIN_ARGS(SAmplitudeLeaderboardScreen) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AAmplitudeDirector>, Director)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

protected:
	virtual void OnBack() override;

private:
	FText GetCellText(int32 Row, int32 Column) const;
	FSlateColor GetRowColor(int32 Row) const;
	void CycleSong(int32 Direction);
	void CycleDifficulty(int32 Direction);

	int32 SongIndex = 0;
	Amp::EDifficulty Difficulty = Amp::EDifficulty::Normal;
};

class SAmplitudeCreditsScreen : public SAmplitudeScreen
{
public:
	SLATE_BEGIN_ARGS(SAmplitudeCreditsScreen) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AAmplitudeDirector>, Director)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

protected:
	virtual void OnBack() override;
};

/** Shown while a song's audio is decoded, or when decoding failed. */
class SAmplitudeLoadingScreen : public SAmplitudeScreen
{
public:
	SLATE_BEGIN_ARGS(SAmplitudeLoadingScreen) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AAmplitudeDirector>, Director)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

protected:
	virtual void OnBack() override;
};
