#pragma once

#include "CoreMinimal.h"
#include "Core/AmpTypes.h"
#include "GameFramework/PlayerController.h"

#include "AmplitudePlayerController.generated.h"

class AAmplitudeDirector;
class FAmplitudeInputTracker;
class SWidget;
class UInputAction;
class UInputMappingContext;

/**
 * Owns gameplay input (spec section 8). Input actions and the mapping context are created at
 * runtime from the active control profile, so remapping needs no assets and applies instantly.
 */
UCLASS()
class AMPLITUDE_API AAmplitudePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AAmplitudePlayerController();

	/** Rebuilds the mapping context from the active control profile. */
	void RebuildInputMappings();

	/** Menus: Slate owns keyboard/gamepad focus and the cursor is visible. */
	void EnterMenuMode(TSharedPtr<SWidget> FocusWidget);
	/** Gameplay: keys go to Enhanced Input, cursor hidden. */
	void EnterGameplayMode();

	bool IsUsingGamepad() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;

private:
	void CreateInputActions();
	void HandleAction(int32 Action);
	void OnMoveLeft();
	void OnMoveRight();
	void OnGemLeft();
	void OnGemMiddle();
	void OnGemRight();
	void OnPause();
	AAmplitudeDirector* GetDirector() const;

	/** Indexed by AmplitudeControls::EAction. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> Actions;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	TSharedPtr<FAmplitudeInputTracker> InputTracker;
};
