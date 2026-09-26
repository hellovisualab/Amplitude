#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Core/AmpSfxSynth.h"
#include "Core/AmpTypes.h"
#include "Data/AmplitudeLeaderboard.h"
#include "Data/AmplitudeSongLibrary.h"
#include "Game/AmplitudeSession.h"
#include "GameFramework/Actor.h"

#include <memory>

#include "AmplitudeDirector.generated.h"

class AAmplitudePlayerController;
class SAmplitudeGameView;
class SAmplitudeScreen;
class SBox;
class SOverlay;
class UAmplitudeSfxComponent;
class UAmplitudeStemPlayerComponent;
class UAmplitudeUserSettings;

UENUM()
enum class EAmplitudeScreen : uint8
{
	None,
	MainMenu,
	SongSelect,
	Settings,
	Leaderboards,
	Credits,
	Loading,
	Playing,
	Paused,
	Results
};

/**
 * Runs the whole game: owns the song library, leaderboard, audio components and the current play
 * session, and drives the screen flow (main menu -> song select -> play <-> pause -> results).
 */
UCLASS()
class AMPLITUDE_API AAmplitudeDirector : public AActor
{
	GENERATED_BODY()

public:
	AAmplitudeDirector();

	virtual void Tick(float DeltaSeconds) override;

	// Screens
	void ShowMainMenu();
	void ShowSongSelect();
	void ShowSettings();
	void CloseSettings();
	void ShowLeaderboards();
	void CloseLeaderboards();
	void ShowCredits();
	void QuitGame();

	// Song flow
	void SelectSong(int32 Index);
	void SelectDifficulty(Amp::EDifficulty Difficulty);
	void StartSelectedSong();
	/** After an audio load failure: play the chart on the wall clock without music. */
	void StartSelectedSongWithoutAudio();
	void RestartSong();
	void PauseGame();
	void ResumeGame();
	void QuitToSongSelect();
	void QuitToMainMenu();
	void RescanSongs();

	// Input from the player controller
	void HandleLaneInput(int32 Lane, double PressedAtSeconds);
	void HandlePauseInput();

	// Settings
	void ApplyAudioSettings();
	void ApplyControlSettings();
	void SaveSettings();

	void PlayUiSound(Amp::ESfx Sfx);

	// Debug (console commands amp.*)
	void SetAutoPlay(bool bEnabled);
	void GrantPowerup(Amp::EPowerupType Type);
	void SetSoloLane(int32 Lane);

	EAmplitudeScreen GetScreen() const { return Screen; }
	const FAmplitudeSongLibrary& GetSongLibrary() const { return SongLibrary; }
	const FAmplitudeLeaderboard& GetLeaderboard() const { return Leaderboard; }
	const FAmplitudeSession* GetSession() const { return Session.Get(); }
	const FAmplitudeRunResult* GetLastResult() const { return LastResult.GetPtrOrNull(); }
	int32 GetSelectedSongIndex() const { return SelectedSongIndex; }
	TSharedPtr<const FAmplitudeSongDefinition> GetSelectedSong() const;
	Amp::EDifficulty GetSelectedDifficulty() const { return SelectedDifficulty; }
	bool IsLoading() const { return bLoading; }
	const FString& GetLoadError() const { return LoadError; }
	/** Seconds left on the "3-2-1" countdown after resuming, 0 when not counting down. */
	double GetResumeCountdownSeconds() const;
	bool IsUsingGamepad() const;
	UAmplitudeUserSettings* GetSettings() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void SetScreen(EAmplitudeScreen NewScreen, TSharedPtr<SAmplitudeScreen> Content);
	void ApplyPendingFocus();
	void BeginSession(std::shared_ptr<const Amp::FSongAudio> Audio);
	void EndSession();
	void TickLoading();
	void TickSession(double Now);
	void FinishSession();
	AAmplitudePlayerController* GetAmplitudeController() const;

	UPROPERTY(VisibleAnywhere, Category = "Amplitude")
	TObjectPtr<UAmplitudeStemPlayerComponent> StemPlayer;

	UPROPERTY(VisibleAnywhere, Category = "Amplitude")
	TObjectPtr<UAmplitudeSfxComponent> SfxPlayer;

	FAmplitudeSongLibrary SongLibrary;
	FAmplitudeLeaderboard Leaderboard;
	TUniquePtr<FAmplitudeSession> Session;
	TOptional<FAmplitudeRunResult> LastResult;

	TSharedPtr<SOverlay> RootWidget;
	TSharedPtr<SAmplitudeGameView> GameView;
	TSharedPtr<SBox> ScreenHost;
	TSharedPtr<SAmplitudeScreen> CurrentScreen;
	int32 FocusRetryFrames = 0;

	EAmplitudeScreen Screen = EAmplitudeScreen::None;
	EAmplitudeScreen SettingsReturnScreen = EAmplitudeScreen::MainMenu;
	EAmplitudeScreen LeaderboardReturnScreen = EAmplitudeScreen::MainMenu;

	int32 SelectedSongIndex = 0;
	Amp::EDifficulty SelectedDifficulty = Amp::EDifficulty::Normal;

	TFuture<FAmplitudeAudioLoadResult> PendingLoad;
	bool bLoading = false;
	FString LoadError;
	FString LoadingSongId;
	FString CachedAudioSongId;
	std::shared_ptr<const Amp::FSongAudio> CachedAudio;

	double ResumeCountdownEndSeconds = 0.0;
	int32 LastCountdownStep = 0;
	double FinishAtSeconds = 0.0;
	double NextLowEnergyAlertSeconds = 0.0;
	bool bAutoPlay = false;
	int32 SoloLane = INDEX_NONE;
};
