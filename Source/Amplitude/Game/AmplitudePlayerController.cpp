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
	// The director points the camera at the 3D stage; possessing the placeholder pawn must not undo that.
	bAutoManageActiveCameraTarget = false;
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
	if (Actions.Num() == AmplitudeControls::NumActions)
	{
		return;
	}
	Actions.Reset();
	for (int32 Action = 0; Action < AmplitudeControls::NumActions; ++Action)
	{
		// Boolean (digital) actions; Started fires once on each press.
		Actions.Add(NewObject<UInputAction>(this));
	}
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
	const FHandler Handlers[AmplitudeControls::NumActions] = {
		&AAmplitudePlayerController::OnMoveLeft, &AAmplitudePlayerController::OnMoveRight,
		&AAmplitudePlayerController::OnGemLeft, &AAmplitudePlayerController::OnGemMiddle, &AAmplitudePlayerController::OnGemRight,
		&AAmplitudePlayerController::OnPause};
	for (int32 Action = 0; Action < AmplitudeControls::NumActions; ++Action)
	{
		EnhancedInput->BindAction(Actions[Action], ETriggerEvent::Started, this, Handlers[Action]);
	}
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
	for (int32 Action = 0; Action < AmplitudeControls::NumActions; ++Action)
	{
		for (const FKey& Key : Profile.GetKeys(Action))
		{
			NewContext->MapKey(Actions[Action], Key);
		}
	}
	// The left stick always steers as well, like the D-pad.
	NewContext->MapKey(Actions[AmplitudeControls::MoveLeft], EKeys::Gamepad_LeftStick_Left);
	NewContext->MapKey(Actions[AmplitudeControls::MoveRight], EKeys::Gamepad_LeftStick_Right);

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
	// Keys still held from the menu (Enter/Space) must not register as gem presses.
	FlushPressedKeys();
}

bool AAmplitudePlayerController::IsUsingGamepad() const
{
	return InputTracker.IsValid() && InputTracker->IsUsingGamepad();
}

void AAmplitudePlayerController::HandleAction(int32 Action)
{
	const double Now = FPlatformTime::Seconds();
	double PressedAt = Now;
	if (InputTracker.IsValid())
	{
		if (const UAmplitudeUserSettings* Settings = UAmplitudeUserSettings::Get())
		{
			TArray<FKey> Keys = Settings->GetActiveProfile().GetKeys(Action);
			if (Action == AmplitudeControls::MoveLeft)
			{
				Keys.Add(EKeys::Gamepad_LeftStick_Left);
			}
			else if (Action == AmplitudeControls::MoveRight)
			{
				Keys.Add(EKeys::Gamepad_LeftStick_Right);
			}
			PressedAt = InputTracker->FindRecentPress(Keys, Now, PressTimestampWindowSeconds);
		}
	}
	if (AAmplitudeDirector* Director = GetDirector())
	{
		Director->HandleActionInput(Action, PressedAt);
	}
}

void AAmplitudePlayerController::OnMoveLeft()
{
	HandleAction(AmplitudeControls::MoveLeft);
}

void AAmplitudePlayerController::OnMoveRight()
{
	HandleAction(AmplitudeControls::MoveRight);
}

void AAmplitudePlayerController::OnGemLeft()
{
	HandleAction(AmplitudeControls::GemLeft);
}

void AAmplitudePlayerController::OnGemMiddle()
{
	HandleAction(AmplitudeControls::GemMiddle);
}

void AAmplitudePlayerController::OnGemRight()
{
	HandleAction(AmplitudeControls::GemRight);
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
