#pragma once

#include "CoreMinimal.h"
#include "Core/AmpSimulation.h"
#include "GameFramework/Actor.h"
#include "Math/RandomStream.h"

#include "AmplitudeStage.generated.h"

class AAmplitudeDirector;
class FAmplitudeSession;
class UCameraComponent;
class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
class USkyLightComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** One (dark) look of the environment; the stage blends from one to the next as the song moves through its sections. */
struct FAmplitudeStagePalette
{
	FLinearColor SkyTop;
	FLinearColor SkyMid;
	FLinearColor Horizon;
	FLinearColor Ground;
	FLinearColor Track;
	/** The moon disc. */
	FLinearColor Sun;
	FLinearColor Hoop;
	FLinearColor Shapes[3];

	static FAmplitudeStagePalette Lerp(const FAmplitudeStagePalette& A, const FAmplitudeStagePalette& B, float Alpha);
};

/**
 * The 3D presentation of the game: a winding six-lane track with gems in three columns, the Beat
 * Blaster, hit targets, powerups, sparks, a sky dome, fog and floating scenery that pulses with the
 * beat. It reads the director's current session every frame and plays an attract loop when there
 * is none (behind the menus). Everything is built from engine shapes and procedural meshes.
 */
UCLASS()
class AMPLITUDE_API AAmplitudeStage : public AActor
{
	GENERATED_BODY()

public:
	AAmplitudeStage();

	virtual void Tick(float DeltaSeconds) override;

	void SetDirector(AAmplitudeDirector* InDirector) { Director = InDirector; }
	/** Visual feedback for a simulation event (sparks, flashes, camera shake). */
	void HandleSimEvent(const Amp::FEvent& Event);
	void ResetEffects();

	/** Where a gem column's target sits on the hit line; HUD pop-ups are anchored here. */
	FVector GetHitPoint(int32 Lane, int32 Column) const;
	FVector GetShipLocation() const { return ShipLocation; }
	/** Projects a world position into the viewport as a 0-1 fraction of its size; false when off screen or behind the camera. */
	bool ProjectToViewport(const FVector& WorldLocation, FVector2D& OutFraction) const;

protected:
	virtual void BeginPlay() override;

private:
	struct FTrackFrame
	{
		FVector Position;
		FVector Forward;
		/** Across the track, towards higher lanes. */
		FVector Lateral;
		/** Out of the track surface. */
		FVector Normal;
	};

	struct FSpark
	{
		FVector Position = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		float Age = 0.0f;
		float Life = 0.5f;
		float Size = 0.1f;
		float Gravity = -900.0f;
		int32 Slot = 0;
	};

	struct FFloater
	{
		FVector Position = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		float Radius = 100.0f;
		float Phase = 0.0f;
		float Spin = 0.0f;
		int32 Kind = 0;
	};

	struct FAttractGem
	{
		double TimeMs = 0.0;
		int32 Lane = 0;
		int32 Column = 1;
	};

	// Construction
	UInstancedStaticMeshComponent* AddInstanced(const FString& Name, UStaticMesh* Mesh, UMaterialInterface* Material, bool bCastShadow);
	UStaticMeshComponent* AddShipPart(const TCHAR* Name, UStaticMesh* Mesh, UMaterialInterface* Material, const FTransform& Relative);
	void BuildScene();
	void BuildShip();
	void BuildSkyDome();
	void ScatterFloaters();
	void RespawnFloater(FFloater& Floater, bool bAnywhere);

	// Per frame
	void UpdateTiming(float DeltaSeconds);
	void UpdatePalette(float DeltaSeconds);
	void UpdateLaneColors();
	void UpdateShip(float DeltaSeconds);
	void UpdateCamera(float DeltaSeconds);
	void UpdateTrackMesh();
	void UpdateHoops();
	void UpdateGems();
	void UpdatePowerups();
	void UpdateTargets(float DeltaSeconds);
	void UpdateSparks(float DeltaSeconds);
	void UpdateFloaters(float DeltaSeconds);
	void UpdateSky();
	void UpdatePostProcess();
	void CollectAttractGems(double FromMs, double ToMs, TArray<FAttractGem>& Out) const;

	// Track geometry
	FTrackFrame GetFrame(double Distance, double Across) const;
	FVector TrackPoint(double Distance, double Across, double Height) const;
	FTransform MakeTrackTransform(double Distance, double Across, double Height, const FVector& Scale, float SpinDegrees = 0.0f) const;
	double DistanceForTime(double TimeMs) const;
	static double LaneCenter(int32 Lane);
	static double ColumnOffset(int32 Column);

	void SpawnSparks(const FVector& Location, int32 Slot, int32 Count, float Speed, float Size, float UpBias);
	void AddShake(float Amount);
	static void SetInstances(UInstancedStaticMeshComponent* Component, const TArray<FTransform>& Transforms);
	const FAmplitudeSession* GetSession() const;
	int32 GetDisplayedShipLane() const;
	static int32 PowerupSlot(Amp::EPowerupType Type);

	TWeakObjectPtr<AAmplitudeDirector> Director;

	UPROPERTY(VisibleAnywhere, Category = "Amplitude")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Amplitude")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, Category = "Amplitude")
	TObjectPtr<UDirectionalLightComponent> SunLight;

	UPROPERTY(VisibleAnywhere, Category = "Amplitude")
	TObjectPtr<UDirectionalLightComponent> FillLight;

	UPROPERTY(VisibleAnywhere, Category = "Amplitude")
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY(VisibleAnywhere, Category = "Amplitude")
	TObjectPtr<UExponentialHeightFogComponent> Fog;

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> TrackMesh;

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> HoopMesh;

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> SkyMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> SunDisc;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> StarInstances;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> GemInstances;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> FadedGemInstances;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> TargetInstances;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> PowerupInstances;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> SparkInstances;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> FloaterInstances;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> ShipRoot;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> ShipEmitters;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Beams;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> ShipGlow;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> ShieldBubble;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> GemMaterials;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> TargetMaterials;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SparkMaterials;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FloaterMaterials;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TrackSurfaceMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TrackGlowMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HoopMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SunMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ShipAccentMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ShipGlowMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BeamMaterial;

	// Timing (song time when a session runs, an attract clock otherwise)
	double SongTimeMs = 0.0;
	double PreviousSongTimeMs = 0.0;
	double ApproachMs = 2000.0;
	double BeatMs = 500.0;
	double FirstBeatMs = 0.0;
	double AttractTimeMs = 0.0;
	double VisualSeconds = 0.0;
	float BeatPulse = 0.0f;
	/** Share of the instruments currently heard (0-1, smoothed); 1 in the menus. */
	float MusicLevel = 1.0f;
	bool bHasSession = false;

	// Track shape
	double BendAcross = 0.0;
	double BendUp = 0.0;

	// Palette
	FAmplitudeStagePalette Palette;
	int32 PaletteIndex = 0;
	FLinearColor LaneColors[Amp::NumLanes];
	double NextSkyCaptureSeconds = 0.0;

	// Ship and camera
	double ShipAcross = 0.0;
	double ShipAcrossVelocity = 0.0;
	double CameraAcross = 0.0;
	FVector ShipLocation = FVector::ZeroVector;
	float ShakeAmount = 0.0f;
	float FovKick = 0.0f;
	int32 AttractShipLane = 2;

	// Effects
	float TargetFlash[Amp::NumLanes][Amp::NumColumns] = {};
	float BeamFlash[Amp::NumColumns] = {};
	float LaneFlash[Amp::NumLanes] = {};
	float MissFlash[Amp::NumLanes] = {};
	float CaptureFlash[Amp::NumLanes] = {};
	TArray<FSpark> Sparks;
	TArray<FFloater> Floaters;
	FRandomStream Random;
	bool bDoubleSidedGeometry = false;

	// Sky dome geometry (only its colours change)
	TArray<FVector> SkyVertices;
	TArray<FVector> SkyNormals;
	TArray<float> SkyElevation;

	TArray<FAttractGem> AttractScratch;
};
