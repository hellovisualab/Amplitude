#include "Game/AmplitudeDirector.h"

#include "Amplitude.h"
#include "Async/Async.h"
#include "Audio/AmplitudeSfxComponent.h"
#include "Audio/AmplitudeStemPlayerComponent.h"
#include "Components/SceneComponent.h"
#include "Core/AmpRules.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Game/AmplitudePlayerController.h"
#include "Game/AmplitudeUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Stage/AmplitudeStage.h"
#include "UI/AmplitudeStyle.h"
#include "UI/SAmplitudeGameView.h"
#include "UI/SAmplitudeMenus.h"
#include "UI/SAmplitudeSettingsScreen.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"

namespace
{
	constexpr double CountdownStepSeconds = 0.4;
	constexpr int32 CountdownSteps = 3;
	constexpr double GameOverResultsDelaySeconds = 1.5;
	constexpr double CompleteResultsDelaySeconds = 2.5;
	constexpr double LowEnergyAlertIntervalSeconds = 1.0;

	AAmplitudeDirector* FindDirector(UWorld* World)
	{
		if (World == nullptr)
		{
			return nullptr;
		}
		TActorIterator<AAmplitudeDirector> It(World);
		return It ? *It : nullptr;
	}

	FAutoConsoleCommandWithWorldAndArgs AutoPlayCommand(
		TEXT("amp.AutoPlay"),
		TEXT("amp.AutoPlay <0|1> - every lane plays itself (useful to check chart/audio sync)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (AAmplitudeDirector* Director = FindDirector(World))
			{
				Director->SetAutoPlay(Args.Num() == 0 || FCString::Atoi(*Args[0]) != 0);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GivePowerupCommand(
		TEXT("amp.GivePowerup"),
		TEXT("amp.GivePowerup <score_2x|lane_cleaner|slow_motion|shield|fever|auto_capture> - applies a powerup immediately."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			AAmplitudeDirector* Director = FindDirector(World);
			const int32 Type = Args.Num() > 0 ? Amp::FindPowerupByKey(StringCast<ANSICHAR>(*Args[0].ToLower()).Get()) : INDEX_NONE;
			if (Director != nullptr && Type != INDEX_NONE)
			{
				Director->GrantPowerup(static_cast<Amp::EPowerupType>(Type));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs SoloLaneCommand(
		TEXT("amp.SoloLane"),
		TEXT("amp.SoloLane <1-6|0> - hear only one instrument track (0 restores the full mix)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (AAmplitudeDirector* Director = FindDirector(World))
			{
				const int32 Lane = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
				Director->SetSoloLane(Lane >= 1 && Lane <= Amp::NumLanes ? Lane - 1 : INDEX_NONE);
			}
		}));
}

AAmplitudeDirector::AAmplitudeDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	// After the player controller has processed this frame's input.
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	StemPlayer = CreateDefaultSubobject<UAmplitudeStemPlayerComponent>(TEXT("StemPlayer"));
	StemPlayer->SetupAttachment(Root);

	SfxPlayer = CreateDefaultSubobject<UAmplitudeSfxComponent>(TEXT("SfxPlayer"));
	SfxPlayer->SetupAttachment(Root);
}

void AAmplitudeDirector::BeginPlay()
{
	Super::BeginPlay();

	UAmplitudeUserSettings* Settings = GetSettings();
	SongLibrary.Scan(Settings != nullptr ? Settings->AdditionalSongDirectories : TArray<FString>());
	Leaderboard.Load();
	if (Settings != nullptr)
	{
		SelectedDifficulty = Settings->GetLastDifficulty();
		SelectedSongIndex = FMath::Max(0, SongLibrary.IndexOfId(Settings->LastSongId));
	}

	StemPlayer->Start();
	SfxPlayer->Start();
	ApplyAudioSettings();

	FActorSpawnParameters StageSpawn;
	StageSpawn.Owner = this;
	StageSpawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Stage = GetWorld()->SpawnActor<AAmplitudeStage>(AAmplitudeStage::StaticClass(), FTransform::Identity, StageSpawn);
	if (Stage != nullptr)
	{
		Stage->SetDirector(this);
		// The stage draws the state this director has just advanced.
		Stage->AddTickPrerequisiteActor(this);
	}
	EnsureStageView();

	TWeakObjectPtr<AAmplitudeDirector> WeakThis(this);
	AmplitudeUI::SetSoundHandler([WeakThis](Amp::ESfx Sfx)
	{
		if (AAmplitudeDirector* Director = WeakThis.Get())
		{
			Director->PlayUiSound(Sfx);
		}
	});

	RootWidget = SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SAssignNew(GameView, SAmplitudeGameView)
			.Director(this)
		]
		+ SOverlay::Slot()
		[
			SAssignNew(ScreenHost, SBox)
		];

	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Viewport->AddViewportWidgetContent(RootWidget.ToSharedRef(), 10);
	}

	ShowMainMenu();
}

void AAmplitudeDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	AmplitudeUI::SetSoundHandler(nullptr);
	if (RootWidget.IsValid())
	{
		if (UGameViewportClient* Viewport = GetWorld() != nullptr ? GetWorld()->GetGameViewport() : nullptr)
		{
			Viewport->RemoveViewportWidgetContent(RootWidget.ToSharedRef());
		}
	}
	CurrentScreen.Reset();
	ScreenHost.Reset();
	GameView.Reset();
	RootWidget.Reset();

	bLoading = false;
	PendingLoad.Reset();
	Session.Reset();
	if (Stage != nullptr)
	{
		Stage->SetDirector(nullptr);
	}
	StemPlayer->Stop();
	SfxPlayer->Stop();
	Super::EndPlay(EndPlayReason);
}

void AAmplitudeDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const double Now = FPlatformTime::Seconds();
	EnsureStageView();
	ApplyPendingFocus();
	if (bLoading)
	{
		TickLoading();
	}
	if (Session.IsValid())
	{
		TickSession(Now);
	}
}

UAmplitudeUserSettings* AAmplitudeDirector::GetSettings() const
{
	return UAmplitudeUserSettings::Get();
}

AAmplitudeStage* AAmplitudeDirector::GetStage() const
{
	return Stage.Get();
}

void AAmplitudeDirector::EnsureStageView()
{
	AAmplitudePlayerController* Controller = GetAmplitudeController();
	AAmplitudeStage* StageActor = Stage.Get();
	if (StageActor != nullptr && Controller != nullptr && Controller->GetViewTarget() != StageActor)
	{
		Controller->SetViewTarget(StageActor);
	}
}

void AAmplitudeDirector::ForwardSimEvent(const Amp::FEvent& Event)
{
	if (Stage != nullptr)
	{
		Stage->HandleSimEvent(Event);
	}
	if (GameView.IsValid())
	{
		GameView->HandleSimEvent(Event);
	}
}

AAmplitudePlayerController* AAmplitudeDirector::GetAmplitudeController() const
{
	return GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController<AAmplitudePlayerController>() : nullptr;
}

TSharedPtr<const FAmplitudeSongDefinition> AAmplitudeDirector::GetSelectedSong() const
{
	return SongLibrary.GetSong(SelectedSongIndex);
}

double AAmplitudeDirector::GetResumeCountdownSeconds() const
{
	return ResumeCountdownEndSeconds > 0.0 ? FMath::Max(0.0, ResumeCountdownEndSeconds - FPlatformTime::Seconds()) : 0.0;
}

bool AAmplitudeDirector::IsUsingGamepad() const
{
	const UAmplitudeUserSettings* Settings = GetSettings();
	if (Settings != nullptr && Settings->ControllerMode != EAmplitudeControllerMode::AutoDetect)
	{
		return Settings->ControllerMode == EAmplitudeControllerMode::Gamepad;
	}
	const AAmplitudePlayerController* Controller = GetAmplitudeController();
	return Controller != nullptr && Controller->IsUsingGamepad();
}

void AAmplitudeDirector::SetScreen(EAmplitudeScreen NewScreen, TSharedPtr<SAmplitudeScreen> Content)
{
	Screen = NewScreen;
	CurrentScreen = Content;
	if (ScreenHost.IsValid())
	{
		ScreenHost->SetContent(Content.IsValid() ? StaticCastSharedRef<SWidget>(Content.ToSharedRef()) : SNullWidget::NullWidget);
	}

	if (AAmplitudePlayerController* Controller = GetAmplitudeController())
	{
		if (NewScreen == EAmplitudeScreen::Playing)
		{
			Controller->EnterGameplayMode();
		}
		else
		{
			Controller->EnterMenuMode(Content.IsValid() ? Content->GetInitialFocus() : nullptr);
		}
	}
	// Newly created widgets have no layout yet, so focus is re-applied over the next frames.
	FocusRetryFrames = Content.IsValid() ? 3 : 0;
}

void AAmplitudeDirector::ApplyPendingFocus()
{
	if (FocusRetryFrames <= 0 || !CurrentScreen.IsValid() || !FSlateApplication::IsInitialized())
	{
		FocusRetryFrames = 0;
		return;
	}
	--FocusRetryFrames;
	const TSharedPtr<SWidget> Focus = CurrentScreen->GetInitialFocus();
	if (Focus.IsValid() && !CurrentScreen->HasFocusedDescendants() && !Focus->HasAnyUserFocus().IsSet())
	{
		FSlateApplication::Get().SetAllUserFocus(Focus, EFocusCause::SetDirectly);
	}
}

void AAmplitudeDirector::ShowMainMenu()
{
	SetScreen(EAmplitudeScreen::MainMenu, SNew(SAmplitudeMainMenu).Director(this));
}

void AAmplitudeDirector::ShowSongSelect()
{
	SetScreen(EAmplitudeScreen::SongSelect, SNew(SAmplitudeSongSelect).Director(this));
}

void AAmplitudeDirector::ShowSettings()
{
	if (Screen != EAmplitudeScreen::Settings)
	{
		SettingsReturnScreen = Screen == EAmplitudeScreen::Paused ? EAmplitudeScreen::Paused : EAmplitudeScreen::MainMenu;
	}
	SetScreen(EAmplitudeScreen::Settings, SNew(SAmplitudeSettingsScreen).Director(this));
}

void AAmplitudeDirector::CloseSettings()
{
	SaveSettings();
	if (SettingsReturnScreen == EAmplitudeScreen::Paused && Session.IsValid())
	{
		SetScreen(EAmplitudeScreen::Paused, SNew(SAmplitudePauseMenu).Director(this));
	}
	else
	{
		ShowMainMenu();
	}
}

void AAmplitudeDirector::ShowLeaderboards()
{
	if (Screen != EAmplitudeScreen::Leaderboards)
	{
		LeaderboardReturnScreen = Screen == EAmplitudeScreen::Results ? EAmplitudeScreen::Results : EAmplitudeScreen::MainMenu;
	}
	SetScreen(EAmplitudeScreen::Leaderboards, SNew(SAmplitudeLeaderboardScreen).Director(this));
}

void AAmplitudeDirector::CloseLeaderboards()
{
	if (LeaderboardReturnScreen == EAmplitudeScreen::Results && LastResult.IsSet())
	{
		SetScreen(EAmplitudeScreen::Results, SNew(SAmplitudeResultsScreen).Director(this));
	}
	else
	{
		ShowMainMenu();
	}
}

void AAmplitudeDirector::ShowCredits()
{
	SetScreen(EAmplitudeScreen::Credits, SNew(SAmplitudeCreditsScreen).Director(this));
}

void AAmplitudeDirector::QuitGame()
{
	SaveSettings();
	UKismetSystemLibrary::QuitGame(this, GetAmplitudeController(), EQuitPreference::Quit, false);
}

void AAmplitudeDirector::SelectSong(int32 Index)
{
	if (SongLibrary.GetSong(Index).IsValid())
	{
		SelectedSongIndex = Index;
	}
}

void AAmplitudeDirector::SelectDifficulty(Amp::EDifficulty Difficulty)
{
	SelectedDifficulty = Difficulty;
	if (UAmplitudeUserSettings* Settings = GetSettings())
	{
		Settings->SetLastDifficulty(Difficulty);
	}
}

void AAmplitudeDirector::StartSelectedSong()
{
	const TSharedPtr<const FAmplitudeSongDefinition> Song = GetSelectedSong();
	if (!Song.IsValid())
	{
		return;
	}

	if (UAmplitudeUserSettings* Settings = GetSettings())
	{
		Settings->LastSongId = Song->Id;
		Settings->SetLastDifficulty(SelectedDifficulty);
		Settings->SaveSettings();
	}

	EndSession();
	LastResult.Reset();
	LoadError.Reset();

	if (!Song->HasAudioConfigured())
	{
		BeginSession(nullptr);
		return;
	}
	if (CachedAudio != nullptr && CachedAudioSongId == Song->Id)
	{
		BeginSession(CachedAudio);
		return;
	}

	// Decoding a multitrack WAV takes a moment; do it off the game thread.
	bLoading = true;
	LoadingSongId = Song->Id;
	PendingLoad = Async(EAsyncExecution::ThreadPool, [Song]()
	{
		return FAmplitudeSongLibrary::LoadAudio(*Song);
	});
	SetScreen(EAmplitudeScreen::Loading, SNew(SAmplitudeLoadingScreen).Director(this));
}

void AAmplitudeDirector::StartSelectedSongWithoutAudio()
{
	EndSession();
	LoadError.Reset();
	BeginSession(nullptr);
}

void AAmplitudeDirector::RestartSong()
{
	StartSelectedSong();
}

void AAmplitudeDirector::TickLoading()
{
	if (!PendingLoad.IsValid() || !PendingLoad.IsReady())
	{
		return;
	}

	const FAmplitudeAudioLoadResult Result = PendingLoad.Get();
	PendingLoad.Reset();
	bLoading = false;

	for (const FString& Warning : Result.Warnings)
	{
		UE_LOG(LogAmplitude, Warning, TEXT("%s"), *Warning);
	}

	const TSharedPtr<const FAmplitudeSongDefinition> Song = GetSelectedSong();
	if (Screen != EAmplitudeScreen::Loading || !Song.IsValid() || Song->Id != LoadingSongId)
	{
		return;
	}

	if (Result.Audio != nullptr)
	{
		CachedAudio = Result.Audio;
		CachedAudioSongId = LoadingSongId;
		BeginSession(CachedAudio);
		return;
	}

	LoadError = Result.Error.IsEmpty() ? FString(TEXT("The song's audio could not be loaded.")) : Result.Error;
	UE_LOG(LogAmplitude, Warning, TEXT("Audio for %s failed to load: %s"), *LoadingSongId, *LoadError);
	SetScreen(EAmplitudeScreen::Loading, SNew(SAmplitudeLoadingScreen).Director(this));
}

void AAmplitudeDirector::BeginSession(std::shared_ptr<const Amp::FSongAudio> Audio)
{
	const TSharedPtr<const FAmplitudeSongDefinition> Song = GetSelectedSong();
	if (!Song.IsValid())
	{
		ShowSongSelect();
		return;
	}

	const UAmplitudeUserSettings* Settings = GetSettings();
	FAmplitudeSessionConfig Config;
	Config.Song = Song;
	Config.Difficulty = SelectedDifficulty;
	Config.Audio = MoveTemp(Audio);
	Config.UserOffsetMs = Settings != nullptr ? Settings->AudioOffsetMs : 0.0;
	Config.Seed = FPlatformTime::Cycles64();
	Config.bAutoPlay = bAutoPlay;

	Session = MakeUnique<FAmplitudeSession>(Config, StemPlayer, SfxPlayer);
	Session->OnEvent = [this](const Amp::FEvent& Event)
	{
		ForwardSimEvent(Event);
	};

	if (GameView.IsValid())
	{
		GameView->ResetEffects();
	}
	if (Stage != nullptr)
	{
		Stage->ResetEffects();
	}
	StemPlayer->SetSoloLane(SoloLane);
	ResumeCountdownEndSeconds = 0.0;
	FinishAtSeconds = 0.0;
	NextLowEnergyAlertSeconds = 0.0;

	Session->Start(FPlatformTime::Seconds());
	SetScreen(EAmplitudeScreen::Playing, nullptr);
}

void AAmplitudeDirector::EndSession()
{
	bLoading = false;
	PendingLoad.Reset();
	if (Session.IsValid())
	{
		Session->StopAudio();
		Session.Reset();
	}
	StemPlayer->Unload();
	ResumeCountdownEndSeconds = 0.0;
	FinishAtSeconds = 0.0;
	if (GameView.IsValid())
	{
		GameView->ResetEffects();
	}
	if (Stage != nullptr)
	{
		Stage->ResetEffects();
	}
}

void AAmplitudeDirector::TickSession(double Now)
{
	const Amp::FSimulation& Simulation = Session->GetSimulation();

	// A rhythm game cannot be played blind: pause when the window loses focus.
	if (Screen == EAmplitudeScreen::Playing && !Session->IsFinished() && FSlateApplication::IsInitialized() && !FSlateApplication::Get().IsActive())
	{
		PauseGame();
		return;
	}

	if (ResumeCountdownEndSeconds > 0.0)
	{
		const double Remaining = ResumeCountdownEndSeconds - Now;
		const int32 Step = FMath::CeilToInt32(Remaining / CountdownStepSeconds);
		if (Step > 0 && Step != LastCountdownStep)
		{
			LastCountdownStep = Step;
			PlayUiSound(Amp::ESfx::Countdown);
		}
		if (Remaining <= 0.0)
		{
			ResumeCountdownEndSeconds = 0.0;
			Session->SetPaused(false, Now);
		}
	}

	Session->Tick(Now);

	if (Screen == EAmplitudeScreen::Playing && !Session->IsPaused() && !Simulation.IsFinished() && Simulation.IsEnergyLow() && Now >= NextLowEnergyAlertSeconds)
	{
		PlayUiSound(Amp::ESfx::EnergyLow);
		NextLowEnergyAlertSeconds = Now + LowEnergyAlertIntervalSeconds;
	}

	if (Simulation.IsFinished() && FinishAtSeconds <= 0.0 && Screen == EAmplitudeScreen::Playing)
	{
		FinishAtSeconds = Now + (Simulation.IsGameOver() ? GameOverResultsDelaySeconds : CompleteResultsDelaySeconds);
	}
	if (FinishAtSeconds > 0.0 && Now >= FinishAtSeconds)
	{
		FinishAtSeconds = 0.0;
		FinishSession();
	}
}

void AAmplitudeDirector::FinishSession()
{
	if (!Session.IsValid())
	{
		return;
	}

	const Amp::FRunSummary Summary = Session->GetSimulation().Summarize();
	FAmplitudeRunResult Result;
	Result.Song = Session->GetSongPtr();
	Result.Difficulty = Session->GetDifficulty();
	Result.Summary = Summary;

	const FString& SongId = Result.Song->Id;
	if (const FAmplitudeScoreEntry* Best = Leaderboard.GetBest(SongId, Result.Difficulty))
	{
		Result.bHadPreviousBest = true;
		Result.PreviousBest = Best->Score;
	}

	const UAmplitudeUserSettings* Settings = GetSettings();
	FAmplitudeScoreEntry Entry;
	Entry.PlayerName = Settings != nullptr ? Settings->PlayerName : FString(TEXT("Player"));
	Entry.Score = Summary.Score;
	Entry.AccuracyPercent = Summary.AccuracyPercent;
	Entry.Perfect = Summary.Perfect + Summary.AutoHits;
	Entry.Good = Summary.Good;
	Entry.Miss = Summary.Miss;
	Entry.bCompleted = Summary.bCompleted;
	Entry.Date = FDateTime::Now();
	Result.LeaderboardRank = Leaderboard.Submit(SongId, Result.Difficulty, Entry);
	Result.bNewRecord = Summary.Score > 0 && (!Result.bHadPreviousBest || Summary.Score > Result.PreviousBest);

	LastResult = Result;
	Session->StopAudio();
	SetScreen(EAmplitudeScreen::Results, SNew(SAmplitudeResultsScreen).Director(this));
}

void AAmplitudeDirector::PauseGame()
{
	if (Screen != EAmplitudeScreen::Playing || !Session.IsValid() || Session->IsFinished())
	{
		return;
	}
	Session->SetPaused(true, FPlatformTime::Seconds());
	ResumeCountdownEndSeconds = 0.0;
	SetScreen(EAmplitudeScreen::Paused, SNew(SAmplitudePauseMenu).Director(this));
}

void AAmplitudeDirector::ResumeGame()
{
	if (!Session.IsValid() || Screen == EAmplitudeScreen::Playing)
	{
		return;
	}
	SetScreen(EAmplitudeScreen::Playing, nullptr);
	// The session stays paused while "3, 2, 1" counts down so the player can get their hands back.
	ResumeCountdownEndSeconds = FPlatformTime::Seconds() + CountdownStepSeconds * CountdownSteps;
	LastCountdownStep = 0;
}

void AAmplitudeDirector::QuitToSongSelect()
{
	EndSession();
	ShowSongSelect();
}

void AAmplitudeDirector::QuitToMainMenu()
{
	EndSession();
	ShowMainMenu();
}

void AAmplitudeDirector::RescanSongs()
{
	const TSharedPtr<const FAmplitudeSongDefinition> Previous = GetSelectedSong();
	const UAmplitudeUserSettings* Settings = GetSettings();
	SongLibrary.Scan(Settings != nullptr ? Settings->AdditionalSongDirectories : TArray<FString>());
	CachedAudio.reset();
	CachedAudioSongId.Reset();
	SelectedSongIndex = Previous.IsValid() ? FMath::Max(0, SongLibrary.IndexOfId(Previous->Id)) : 0;
}

void AAmplitudeDirector::HandleActionInput(int32 Action, double PressedAtSeconds)
{
	if (Action == AmplitudeControls::Pause)
	{
		HandlePauseInput();
		return;
	}
	if (Screen != EAmplitudeScreen::Playing || !Session.IsValid() || ResumeCountdownEndSeconds > 0.0)
	{
		return;
	}
	switch (Action)
	{
	case AmplitudeControls::MoveLeft:
		Session->QueueStep(-1, PressedAtSeconds);
		break;
	case AmplitudeControls::MoveRight:
		Session->QueueStep(1, PressedAtSeconds);
		break;
	case AmplitudeControls::GemLeft:
	case AmplitudeControls::GemMiddle:
	case AmplitudeControls::GemRight:
		Session->QueueFire(Action - AmplitudeControls::GemLeft, PressedAtSeconds);
		break;
	default:
		break;
	}
}

void AAmplitudeDirector::HandleLaneJump(int32 Lane, double PressedAtSeconds)
{
	if (Screen == EAmplitudeScreen::Playing && Session.IsValid() && ResumeCountdownEndSeconds <= 0.0)
	{
		Session->QueueJump(Lane, PressedAtSeconds);
	}
}

void AAmplitudeDirector::HandlePauseInput()
{
	if (Screen == EAmplitudeScreen::Playing)
	{
		PauseGame();
	}
	else if (Screen == EAmplitudeScreen::Paused)
	{
		ResumeGame();
	}
}

void AAmplitudeDirector::ApplyAudioSettings()
{
	if (const UAmplitudeUserSettings* Settings = GetSettings())
	{
		StemPlayer->SetMusicGain(Settings->GetMusicGain());
		SfxPlayer->SetSfxGain(Settings->GetSfxGain());
	}
}

void AAmplitudeDirector::ApplyControlSettings()
{
	if (AAmplitudePlayerController* Controller = GetAmplitudeController())
	{
		Controller->RebuildInputMappings();
	}
}

void AAmplitudeDirector::SaveSettings()
{
	if (UAmplitudeUserSettings* Settings = GetSettings())
	{
		Settings->SaveSettings();
	}
}

void AAmplitudeDirector::PlayUiSound(Amp::ESfx Sfx)
{
	SfxPlayer->Play(Sfx);
}

void AAmplitudeDirector::SetAutoPlay(bool bEnabled)
{
	bAutoPlay = bEnabled;
	if (Session.IsValid())
	{
		Session->SetAutoPlay(bEnabled);
	}
	UE_LOG(LogAmplitude, Log, TEXT("Auto-play %s"), bEnabled ? TEXT("enabled") : TEXT("disabled"));
}

void AAmplitudeDirector::GrantPowerup(Amp::EPowerupType Type)
{
	if (Session.IsValid())
	{
		Session->GrantPowerup(Type);
	}
}

void AAmplitudeDirector::SetSoloLane(int32 Lane)
{
	SoloLane = Lane;
	StemPlayer->SetSoloLane(Lane);
}
