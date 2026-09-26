#include "Game/AmplitudePlayerController.h"

#include "Amplitude.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "Game/AmplitudeDirector.h"
#include "Game/AmplitudeGameMode.h"
#include "Game/AmplitudeInputTracker.h"
#include "Game/AmplitudeUserSettings.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "InputMappingContext.h"

namespace
{
	/** A key-down seen by Slate this recently belongs to the Enhanced Input event being handled. */
	constexpr double PressTimestampWindowSeconds = 0.1;
}

AAmplitudePlayerController::AAmplitudePlayerController()
{
	bShowMouseCursor = true;
}

void AAmplitudePlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalController() && FSlateApplication::IsInitialized())
	{
		InputTracker = MakeShared<FAmplitudeInputTracker>();
		FSlateApplication::Get().RegisterInputPreProcessor(InputTracker);
	}
	RebuildInputMappings();
}

void AAmplitudePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (InputTracker.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(InputTracker);
	}
	InputTracker.Reset();
	Super::EndPlay(EndPlayReason);
}

void AAmplitudePlayerController::CreateInputActions()
{
	if (LaneActions.Num() == Amp::NumLanes && PauseAction != nullptr)
	{
		return;
	}
	LaneActions.Reset();
	for (int32 Lane = 0; Lane < Amp::NumLanes; ++Lane)
	{
		// Boolean (digital) actions; Started fires once on each press.
		LaneActions.Add(NewObject<UInputAction>(this));
	}
	PauseAction = NewObject<UInputAction>(this);
}

void AAmplitudePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	CreateInputActions();

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (EnhancedInput == nullptr)
	{
		UE_LOG(LogAmplitude, Error, TEXT("Enhanced Input is not the default input component; check Config/DefaultInput.ini"));
		return;
	}

	using FHandler = void (AAmplitudePlayerController::*)();
	const FHandler Handlers[Amp::NumLanes] = {
		&AAmplitudePlayerController::OnLane1, &AAmplitudePlayerController::OnLane2, &AAmplitudePlayerController::OnLane3,
		&AAmplitudePlayerController::OnLane4, &AAmplitudePlayerController::OnLane5, &AAmplitudePlayerController::OnLane6};
	for (int32 Lane = 0; Lane < Amp::NumLanes; ++Lane)
	{
		EnhancedInput->BindAction(LaneActions[Lane], ETriggerEvent::Started, this, Handlers[Lane]);
	}
	EnhancedInput->BindAction(PauseAction, ETriggerEvent::Started, this, &AAmplitudePlayerController::OnPause);
}

void AAmplitudePlayerController::RebuildInputMappings()
{
	CreateInputActions();

	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer != nullptr ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	const UAmplitudeUserSettings* Settings = UAmplitudeUserSettings::Get();
	if (Subsystem == nullptr || Settings == nullptr)
	{
		return;
	}

	const FAmplitudeControlProfile& Profile = Settings->GetActiveProfile();
	UInputMappingContext* NewContext = NewObject<UInputMappingContext>(this);
	for (int32 Lane = 0; Lane < Amp::NumLanes; ++Lane)
	{
		for (const FKey& Key : Profile.GetLaneKeys(Lane))
		{
			NewContext->MapKey(LaneActions[Lane], Key);
		}
	}
	for (const FKey& Key : Profile.GetPauseKeys())
	{
		NewContext->MapKey(PauseAction, Key);
	}

	if (MappingContext != nullptr)
	{
		Subsystem->RemoveMappingContext(MappingContext);
	}
	MappingContext = NewContext;
	Subsystem->AddMappingContext(MappingContext, 0);
}

void AAmplitudePlayerController::EnterMenuMode(TSharedPtr<SWidget> FocusWidget)
{
	FInputModeUIOnly Mode;
	if (FocusWidget.IsValid())
	{
		Mode.SetWidgetToFocus(FocusWidget);
	}
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
	SetShowMouseCursor(true);
}

void AAmplitudePlayerController::EnterGameplayMode()
{
	SetInputMode(FInputModeGameOnly());
	SetShowMouseCursor(false);
	// Keys still held from the menu (Enter/Space) must not register as lane presses.
	FlushPressedKeys();
}

bool AAmplitudePlayerController::IsUsingGamepad() const
{
	return InputTracker.IsValid() && InputTracker->IsUsingGamepad();
}

void AAmplitudePlayerController::HandleLane(int32 Lane)
{
	const double Now = FPlatformTime::Seconds();
	double PressedAt = Now;
	if (InputTracker.IsValid())
	{
		if (const UAmplitudeUserSettings* Settings = UAmplitudeUserSettings::Get())
		{
			PressedAt = InputTracker->FindRecentPress(Settings->GetActiveProfile().GetLaneKeys(Lane), Now, PressTimestampWindowSeconds);
		}
	}
	if (AAmplitudeDirector* Director = GetDirector())
	{
		Director->HandleLaneInput(Lane, PressedAt);
	}
}

void AAmplitudePlayerController::OnPause()
{
	if (AAmplitudeDirector* Director = GetDirector())
	{
		Director->HandlePauseInput();
	}
}

AAmplitudeDirector* AAmplitudePlayerController::GetDirector() const
{
	const UWorld* World = GetWorld();
	const AAmplitudeGameMode* GameMode = World != nullptr ? World->GetAuthGameMode<AAmplitudeGameMode>() : nullptr;
	return GameMode != nullptr ? GameMode->GetDirector() : nullptr;
}
