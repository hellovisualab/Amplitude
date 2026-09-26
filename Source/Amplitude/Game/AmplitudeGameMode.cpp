#include "Game/AmplitudeGameMode.h"

#include "Engine/World.h"
#include "Game/AmplitudeDirector.h"
#include "Game/AmplitudePlayerController.h"
#include "GameFramework/Pawn.h"

AAmplitudeGameMode::AAmplitudeGameMode()
{
	PlayerControllerClass = AAmplitudePlayerController::StaticClass();
	// The Beat Blaster lives in the simulation and is drawn by the UI; the pawn is only a placeholder.
	DefaultPawnClass = APawn::StaticClass();
}

void AAmplitudeGameMode::StartPlay()
{
	Super::StartPlay();

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Director = GetWorld()->SpawnActor<AAmplitudeDirector>(AAmplitudeDirector::StaticClass(), FTransform::Identity, SpawnParameters);
}
