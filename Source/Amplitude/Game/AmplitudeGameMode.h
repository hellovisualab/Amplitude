#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "AmplitudeGameMode.generated.h"

class AAmplitudeDirector;

/** Default game mode: spawns the director, which runs the menus and songs. */
UCLASS()
class AMPLITUDE_API AAmplitudeGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AAmplitudeGameMode();

	virtual void StartPlay() override;

	AAmplitudeDirector* GetDirector() const { return Director; }

private:
	UPROPERTY(Transient)
	TObjectPtr<AAmplitudeDirector> Director;
};
