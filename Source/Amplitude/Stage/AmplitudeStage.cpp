#include "Stage/AmplitudeStage.h"

#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/AmplitudeSongLibrary.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Game/AmplitudeDirector.h"
#include "Game/AmplitudeSession.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "Stage/AmplitudeMaterials.h"
#include "UI/AmplitudeStyle.h"

#include <algorithm>

namespace
{
	using AmplitudeMaterials::EKind;

	// Layout in Unreal units (cm). X runs down the track, the hit line is at X = 0.
	constexpr double TrackLength = 7000.0;
	constexpr double BehindLength = 1300.0;
	constexpr double LaneWidth = 230.0;
	constexpr double LaneGap = 46.0;
	constexpr double ColumnSpacing = 70.0;
	/** The track is a shallow half-pipe: outer lanes rise towards the camera. */
	constexpr double ArcRadius = 2600.0;
	constexpr double GemHeight = 24.0;
	constexpr int32 TrackSegments = 70;
	constexpr double HoopRadius = 1180.0;
	constexpr double SkyRadius = 400000.0;

	constexpr int32 MaxSparks = 700;
	constexpr int32 GoldSlot = Amp::NumLanes;
	constexpr int32 WhiteSlot = Amp::NumLanes + 1;
	constexpr int32 RedSlot = Amp::NumLanes + 2;
	constexpr int32 FirstPowerupSlot = Amp::NumLanes + 3;
	constexpr int32 NumSparkSlots = FirstPowerupSlot + Amp::NumPowerupTypes;

	constexpr int32 NumFloaters = 56;
	constexpr int32 NumStars = 420;
	constexpr double StarDistance = 300000.0;
	constexpr int32 NumFloaterKinds = 4;
	constexpr double FloaterNear = -9000.0;
	constexpr double FloaterFar = 42000.0;

	const FName ColorParam(TEXT("Color"));
	const FName GlowParam(TEXT("Glow"));

	TAutoConsoleVariable<float> CVarStageExposure(
		TEXT("amp.Exposure"),
		0.6f,
		TEXT("Exposure compensation (in stops) of the 3D stage."));

	FLinearColor Hex(const TCHAR* Code)
	{
		return FLinearColor(FColor::FromHex(Code));
	}

	FAmplitudeStagePalette MakePalette(const TCHAR* SkyTop, const TCHAR* SkyMid, const TCHAR* Horizon, const TCHAR* Ground, const TCHAR* Track,
		const TCHAR* Sun, const TCHAR* Hoop, const TCHAR* ShapeA, const TCHAR* ShapeB, const TCHAR* ShapeC)
	{
		FAmplitudeStagePalette Palette;
		Palette.SkyTop = Hex(SkyTop);
		Palette.SkyMid = Hex(SkyMid);
		Palette.Horizon = Hex(Horizon);
		Palette.Ground = Hex(Ground);
		Palette.Track = Hex(Track);
		Palette.Sun = Hex(Sun);
		Palette.Hoop = Hex(Hoop);
		Palette.Shapes[0] = Hex(ShapeA);
		Palette.Shapes[1] = Hex(ShapeB);
		Palette.Shapes[2] = Hex(ShapeC);
		return Palette;
	}

	/**
	 * Dark looks, one per song section: midnight blue, plum, deep teal and ember. Only the gems, lane
	 * edges and effects carry bright colour; everything else stays close to black.
	 */
	const FAmplitudeStagePalette& GetPalette(int32 Index)
	{
		static const FAmplitudeStagePalette Palettes[] = {
			MakePalette(TEXT("030409"), TEXT("080B16"), TEXT("121829"), TEXT("040509"), TEXT("171B26"), TEXT("9FB2FF"), TEXT("27304D"), TEXT("1C2336"), TEXT("241F3A"), TEXT("17283B")),
			MakePalette(TEXT("050308"), TEXT("0F0A18"), TEXT("21152E"), TEXT("060408"), TEXT("1A1522"), TEXT("E0B8FF"), TEXT("35284D"), TEXT("2A1F3D"), TEXT("35202F"), TEXT("1D2135")),
			MakePalette(TEXT("020506"), TEXT("051116"), TEXT("0B212B"), TEXT("030607"), TEXT("121B20"), TEXT("A8F0FF"), TEXT("1C3743"), TEXT("142C33"), TEXT("182636"), TEXT("20303A")),
			MakePalette(TEXT("060304"), TEXT("130909"), TEXT("26120F"), TEXT("070405"), TEXT("1C1516"), TEXT("FFC4A8"), TEXT("412721"), TEXT("34201D"), TEXT("2A1B21"), TEXT("3A2A20")),
		};
		constexpr int32 Count = UE_ARRAY_COUNT(Palettes);
		return Palettes[((Index % Count) + Count) % Count];
	}
	constexpr int32 NumPalettes = 4;

	uint32 HashInts(int64 A, int32 B)
	{
		uint32 Hash = static_cast<uint32>(A) * 73856093u ^ static_cast<uint32>(A >> 32) * 83492791u ^ static_cast<uint32>(B) * 19349663u;
		Hash ^= Hash >> 13;
		Hash *= 0x5bd1e995u;
		Hash ^= Hash >> 15;
		return Hash;
	}

	template <typename TComponent>
	TComponent* NewPart(AActor* Actor, USceneComponent* Parent, const TCHAR* Name)
	{
		TComponent* Component = NewObject<TComponent>(Actor, FName(Name));
		Component->SetupAttachment(Parent);
		Component->SetMobility(EComponentMobility::Movable);
		if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
		{
			Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Primitive->SetGenerateOverlapEvents(false);
		}
		Component->RegisterComponent();
		return Component;
	}

	/** Procedural mesh buffers for one section. */
	struct FMeshBuffers
	{
		TArray<FVector> Vertices;
		TArray<FVector> Normals;
		TArray<FLinearColor> Colors;
		TArray<int32> Triangles;

		void Reset()
		{
			Vertices.Reset();
			Normals.Reset();
			Colors.Reset();
			Triangles.Reset();
		}

		/** Quad from a grid: A/B along the first row, C/D the next row (A-C and B-D run the same way). */
		void AddQuad(int32 A, int32 B, int32 C, int32 D, bool bDoubleSided)
		{
			Triangles.Append({A, B, C, B, D, C});
			if (bDoubleSided)
			{
				Triangles.Append({A, C, B, B, C, D});
			}
		}

		void Upload(UProceduralMeshComponent* Mesh, int32 Section) const
		{
			if (Vertices.Num() == 0 || Triangles.Num() == 0)
			{
				Mesh->ClearMeshSection(Section);
				return;
			}
			Mesh->CreateMeshSection_LinearColor(Section, Vertices, Triangles, Normals, TArray<FVector2D>(), Colors, TArray<FProcMeshTangent>(), false);
		}
	};

	FMeshBuffers TrackSurface;
	FMeshBuffers TrackGlow;
	FMeshBuffers TrackBeats;
	FMeshBuffers TrackHitLine;
	FMeshBuffers Hoops;
}

FAmplitudeStagePalette FAmplitudeStagePalette::Lerp(const FAmplitudeStagePalette& A, const FAmplitudeStagePalette& B, float Alpha)
{
	FAmplitudeStagePalette Result;
	Result.SkyTop = FMath::Lerp(A.SkyTop, B.SkyTop, Alpha);
	Result.SkyMid = FMath::Lerp(A.SkyMid, B.SkyMid, Alpha);
	Result.Horizon = FMath::Lerp(A.Horizon, B.Horizon, Alpha);
	Result.Ground = FMath::Lerp(A.Ground, B.Ground, Alpha);
	Result.Track = FMath::Lerp(A.Track, B.Track, Alpha);
	Result.Sun = FMath::Lerp(A.Sun, B.Sun, Alpha);
	Result.Hoop = FMath::Lerp(A.Hoop, B.Hoop, Alpha);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Result.Shapes[Index] = FMath::Lerp(A.Shapes[Index], B.Shapes[Index], Alpha);
	}
	return Result;
}

AAmplitudeStage::AAmplitudeStage()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = SceneRoot;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SceneRoot);
	Camera->SetFieldOfView(72.0f);
	Camera->bConstrainAspectRatio = false;

	SunLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	SunLight->SetupAttachment(SceneRoot);
	SunLight->SetMobility(EComponentMobility::Movable);
	SunLight->SetRelativeRotation(FRotator(-42.0f, 25.0f, 0.0f));
	SunLight->SetIntensity(2.2f);
	SunLight->SetCastShadows(true);

	FillLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Fill"));
	FillLight->SetupAttachment(SceneRoot);
	FillLight->SetMobility(EComponentMobility::Movable);
	FillLight->SetRelativeRotation(FRotator(-14.0f, 205.0f, 0.0f));
	FillLight->SetIntensity(0.6f);
	FillLight->SetCastShadows(false);

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(SceneRoot);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->SourceType = ESkyLightSourceType::SLS_CapturedScene;
	// Only the dome and the sun count as sky; the track and scenery are closer than this.
	SkyLight->SkyDistanceThreshold = 150000.0f;
	SkyLight->SetIntensity(0.35f);

	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(SceneRoot);
	Fog->SetRelativeLocation(FVector(0.0, 0.0, -1500.0));
	Fog->SetFogDensity(0.04f);
	Fog->SetFogHeightFalloff(0.12f);
	Fog->SetStartDistance(1800.0f);
	Fog->SetFogMaxOpacity(0.92f);
	// The sky dome and sun sit beyond this, so they keep their colours.
	Fog->FogCutoffDistance = 150000.0f;
}

void AAmplitudeStage::BeginPlay()
{
	Super::BeginPlay();

	Random.Initialize(0x51A6E);
	Palette = GetPalette(0);
	for (int32 Lane = 0; Lane < Amp::NumLanes; ++Lane)
	{
		LaneColors[Lane] = AmplitudeStyle::GetLaneColor(Lane);
	}
	ShipAcross = LaneCenter(AttractShipLane);
	CameraAcross = ShipAcross * 0.82;

	BuildScene();
	ResetEffects();
}

// ---------------------------------------------------------------------------------------------
// Construction

UInstancedStaticMeshComponent* AAmplitudeStage::AddInstanced(const FString& Name, UStaticMesh* Mesh, UMaterialInterface* Material, bool bCastShadow)
{
	UInstancedStaticMeshComponent* Component = NewPart<UInstancedStaticMeshComponent>(this, SceneRoot, *Name);
	Component->SetStaticMesh(Mesh);
	Component->SetMaterial(0, Material);
	Component->SetCastShadow(bCastShadow);
	return Component;
}

UStaticMeshComponent* AAmplitudeStage::AddShipPart(const TCHAR* Name, UStaticMesh* Mesh, UMaterialInterface* Material, const FTransform& Relative)
{
	UStaticMeshComponent* Part = NewPart<UStaticMeshComponent>(this, ShipRoot, Name);
	Part->SetStaticMesh(Mesh);
	Part->SetMaterial(0, Material);
	Part->SetRelativeTransform(Relative);
	return Part;
}

void AAmplitudeStage::BuildScene()
{
	bDoubleSidedGeometry = !AmplitudeMaterials::AreGenerated();
	UStaticMesh* Sphere = AmplitudeMaterials::GetSphere();
	UStaticMesh* Cube = AmplitudeMaterials::GetCube();
	UStaticMesh* Cylinder = AmplitudeMaterials::GetCylinder();

	// Track: 0 lane surfaces, 1 glowing lane edges, 2 beat lines, 3 hit line.
	TrackMesh = NewPart<UProceduralMeshComponent>(this, SceneRoot, TEXT("Track"));
	TrackMesh->SetCastShadow(false);
	TrackSurfaceMaterial = AmplitudeMaterials::Create(EKind::Lit, this, FLinearColor::White, 0.1f, 0.22f);
	TrackGlowMaterial = AmplitudeMaterials::Create(EKind::Unlit, this, FLinearColor::White, 2.2f);
	TrackMesh->SetMaterial(0, TrackSurfaceMaterial);
	TrackMesh->SetMaterial(1, TrackGlowMaterial);
	TrackMesh->SetMaterial(2, TrackSurfaceMaterial);
	TrackMesh->SetMaterial(3, TrackGlowMaterial);

	HoopMesh = NewPart<UProceduralMeshComponent>(this, SceneRoot, TEXT("Hoops"));
	HoopMesh->SetCastShadow(false);
	HoopMaterial = AmplitudeMaterials::Create(EKind::Lit, this, FLinearColor::White, 0.45f, 0.25f);
	HoopMesh->SetMaterial(0, HoopMaterial);

	SkyMesh = NewPart<UProceduralMeshComponent>(this, SceneRoot, TEXT("Sky"));
	SkyMesh->SetCastShadow(false);
	SkyMesh->SetMaterial(0, AmplitudeMaterials::Create(EKind::Unlit, this, FLinearColor::White, 1.0f));
	BuildSkyDome();

	SunDisc = NewPart<UStaticMeshComponent>(this, SceneRoot, TEXT("SunDisc"));
	SunDisc->SetStaticMesh(Sphere);
	// A small pale moon low over the track.
	SunMaterial = AmplitudeMaterials::Create(EKind::Unlit, this, Palette.Sun, 1.3f);
	SunDisc->SetMaterial(0, SunMaterial);
	SunDisc->SetCastShadow(false);
	SunDisc->SetWorldLocation(FVector(300000.0, -70000.0, 34000.0));
	SunDisc->SetWorldScale3D(FVector(110.0));

	StarInstances = AddInstanced(TEXT("Stars"), Sphere, AmplitudeMaterials::Create(EKind::Additive, this, FLinearColor(0.85f, 0.9f, 1.0f), 2.5f), false);
	TArray<FTransform> Stars;
	for (int32 Index = 0; Index < NumStars; ++Index)
	{
		// Scattered over the upper sky, a little denser near the horizon.
		const double Elevation = FMath::Asin(FMath::Pow(Random.FRand(), 1.6f) * 0.98 + 0.02);
		const double Azimuth = Random.FRandRange(0.0f, 6.2832f);
		const FVector Direction(FMath::Cos(Elevation) * FMath::Cos(Azimuth), FMath::Cos(Elevation) * FMath::Sin(Azimuth), FMath::Sin(Elevation));
		Stars.Add(FTransform(FQuat::Identity, Direction * StarDistance, FVector(Random.FRandRange(4.0f, 11.0f))));
	}
	SetInstances(StarInstances, Stars);

	for (int32 Lane = 0; Lane < Amp::NumLanes; ++Lane)
	{
		GemMaterials.Add(AmplitudeMaterials::Create(EKind::Lit, this, LaneColors[Lane], 0.9f, 0.15f));
		GemInstances.Add(AddInstanced(FString::Printf(TEXT("Gems%d"), Lane), Sphere, GemMaterials[Lane], true));

		TargetMaterials.Add(AmplitudeMaterials::Create(EKind::Lit, this, LaneColors[Lane], 0.25f, 0.3f));
		TargetInstances.Add(AddInstanced(FString::Printf(TEXT("Targets%d"), Lane), Cylinder, TargetMaterials[Lane], false));
	}
	FadedGemInstances = AddInstanced(TEXT("FadedGems"), Sphere, AmplitudeMaterials::Create(EKind::Lit, this, FLinearColor(0.12f, 0.12f, 0.15f), 0.0f, 0.6f), false);

	for (int32 Type = 0; Type < Amp::NumPowerupTypes; ++Type)
	{
		const FLinearColor Color = AmplitudeStyle::GetPowerupColor(static_cast<Amp::EPowerupType>(Type));
		PowerupInstances.Add(AddInstanced(FString::Printf(TEXT("Powerups%d"), Type), Cube, AmplitudeMaterials::Create(EKind::Lit, this, Color, 1.2f, 0.15f), true));
	}

	for (int32 Slot = 0; Slot < NumSparkSlots; ++Slot)
	{
		FLinearColor Color = FLinearColor::White;
		if (Slot < Amp::NumLanes)
		{
			Color = LaneColors[Slot];
		}
		else if (Slot == GoldSlot)
		{
			Color = AmplitudeStyle::Perfect;
		}
		else if (Slot == RedSlot)
		{
			Color = AmplitudeStyle::Miss;
		}
		else if (Slot >= FirstPowerupSlot)
		{
			Color = AmplitudeStyle::GetPowerupColor(static_cast<Amp::EPowerupType>(Slot - FirstPowerupSlot));
		}
		SparkMaterials.Add(AmplitudeMaterials::Create(EKind::Additive, this, Color, 3.0f));
		SparkInstances.Add(AddInstanced(FString::Printf(TEXT("Sparks%d"), Slot), Sphere, SparkMaterials[Slot], false));
	}

	for (int32 Kind = 0; Kind < NumFloaterKinds; ++Kind)
	{
		FloaterMaterials.Add(AmplitudeMaterials::Create(EKind::Lit, this, Palette.Shapes[Kind % 3], 0.05f, 0.45f));
		FloaterInstances.Add(AddInstanced(FString::Printf(TEXT("Floaters%d"), Kind), Kind == NumFloaterKinds - 1 ? Cube : Sphere, FloaterMaterials[Kind], false));
	}

	BuildShip();
	ScatterFloaters();
}

void AAmplitudeStage::BuildShip()
{
	UStaticMesh* Sphere = AmplitudeMaterials::GetSphere();
	UStaticMesh* Cube = AmplitudeMaterials::GetCube();
	UStaticMesh* Cylinder = AmplitudeMaterials::GetCylinder();

	ShipRoot = NewPart<USceneComponent>(this, SceneRoot, TEXT("Ship"));

	UMaterialInstanceDynamic* Hull = AmplitudeMaterials::Create(EKind::Lit, this, Hex(TEXT("2A2F3D")), 0.02f, 0.2f);
	UMaterialInstanceDynamic* Glass = AmplitudeMaterials::Create(EKind::Lit, this, Hex(TEXT("0B0E16")), 0.0f, 0.06f);
	ShipAccentMaterial = AmplitudeMaterials::Create(EKind::Lit, this, LaneColors[AttractShipLane], 1.2f, 0.25f);
	ShipGlowMaterial = AmplitudeMaterials::Create(EKind::Additive, this, LaneColors[AttractShipLane], 1.0f);
	BeamMaterial = AmplitudeMaterials::Create(EKind::Additive, this, LaneColors[AttractShipLane], 3.0f);

	AddShipPart(TEXT("ShipHull"), Sphere, Hull, FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector(1.05, 0.9, 0.26)));
	AddShipPart(TEXT("ShipWing"), Cube, Hull, FTransform(FRotator::ZeroRotator, FVector(-12.0, 0.0, -3.0), FVector(0.42, 2.25, 0.05)));
	AddShipPart(TEXT("ShipCanopy"), Sphere, Glass, FTransform(FRotator::ZeroRotator, FVector(12.0, 0.0, 13.0), FVector(0.4, 0.3, 0.18)));
	AddShipPart(TEXT("ShipFin"), Cube, ShipAccentMaterial, FTransform(FRotator::ZeroRotator, FVector(-38.0, 0.0, 16.0), FVector(0.3, 0.04, 0.26)));

	const TCHAR* const EmitterNames[Amp::NumColumns] = {TEXT("ShipEmitterL"), TEXT("ShipEmitterM"), TEXT("ShipEmitterR")};
	const TCHAR* const BeamNames[Amp::NumColumns] = {TEXT("ShipBeamL"), TEXT("ShipBeamM"), TEXT("ShipBeamR")};
	for (int32 Column = 0; Column < Amp::NumColumns; ++Column)
	{
		const double Across = ColumnOffset(Column);
		ShipEmitters.Add(AddShipPart(EmitterNames[Column], Cylinder, ShipAccentMaterial, FTransform(FRotator(-90.0f, 0.0f, 0.0f), FVector(34.0, Across, -2.0), FVector(0.2, 0.2, 0.5))));
		UStaticMeshComponent* Beam = AddShipPart(BeamNames[Column], Cylinder, BeamMaterial, FTransform(FRotator(-90.0f, 0.0f, 0.0f), FVector(160.0, Across, 0.0), FVector(0.1, 0.1, 2.4)));
		Beam->SetCastShadow(false);
		Beam->SetVisibility(false);
		Beams.Add(Beam);
	}

	ShipGlow = AddShipPart(TEXT("ShipGlow"), Sphere, ShipGlowMaterial, FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, -14.0), FVector(2.8, 2.8, 0.1)));
	ShipGlow->SetCastShadow(false);

	ShieldBubble = AddShipPart(TEXT("ShipShield"), Sphere, AmplitudeMaterials::Create(EKind::Additive, this, AmplitudeStyle::Shield, 0.5f), FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 6.0), FVector(3.0, 3.0, 1.3)));
	ShieldBubble->SetCastShadow(false);
	ShieldBubble->SetVisibility(false);
}

void AAmplitudeStage::BuildSkyDome()
{
	constexpr int32 Rings = 24;
	constexpr int32 Sectors = 40;
	SkyVertices.Reset();
	SkyNormals.Reset();
	SkyElevation.Reset();
	TArray<FLinearColor> Colors;
	for (int32 Ring = 0; Ring <= Rings; ++Ring)
	{
		const double Phi = -UE_DOUBLE_HALF_PI + UE_DOUBLE_PI * Ring / Rings;
		for (int32 Sector = 0; Sector <= Sectors; ++Sector)
		{
			const double Theta = UE_DOUBLE_TWO_PI * Sector / Sectors;
			const FVector Direction(FMath::Cos(Phi) * FMath::Cos(Theta), FMath::Cos(Phi) * FMath::Sin(Theta), FMath::Sin(Phi));
			SkyVertices.Add(Direction * SkyRadius);
			SkyNormals.Add(-Direction);
			SkyElevation.Add(static_cast<float>(Direction.Z));
			Colors.Add(FLinearColor::White);
		}
	}

	TArray<int32> Triangles;
	for (int32 Ring = 0; Ring < Rings; ++Ring)
	{
		for (int32 Sector = 0; Sector < Sectors; ++Sector)
		{
			const int32 A = Ring * (Sectors + 1) + Sector;
			const int32 C = A + Sectors + 1;
			// Seen from inside the dome, so both windings are emitted.
			Triangles.Append({A, A + 1, C, A + 1, C + 1, C, A, C, A + 1, A + 1, C, C + 1});
		}
	}
	SkyMesh->CreateMeshSection_LinearColor(0, SkyVertices, Triangles, SkyNormals, TArray<FVector2D>(), Colors, TArray<FProcMeshTangent>(), false);
}

void AAmplitudeStage::ScatterFloaters()
{
	Floaters.SetNum(NumFloaters);
	for (FFloater& Floater : Floaters)
	{
		RespawnFloater(Floater, true);
	}
}

void AAmplitudeStage::RespawnFloater(FFloater& Floater, bool bAnywhere)
{
	const double Side = Random.FRand() < 0.5f ? -1.0 : 1.0;
	const double Across = Random.FRandRange(3400.0f, 20000.0f);
	Floater.Position.X = bAnywhere ? Random.FRandRange(FloaterNear, FloaterFar) : FloaterFar - Random.FRandRange(0.0f, 4000.0f);
	Floater.Position.Y = Side * Across;
	Floater.Position.Z = Random.FRandRange(-2600.0f, 5200.0f);
	Floater.Radius = Random.FRandRange(110.0f, 520.0f) + static_cast<float>(Across) * 0.025f;
	Floater.Phase = Random.FRandRange(0.0f, 6.28f);
	Floater.Spin = Random.FRandRange(-25.0f, 25.0f);
	Floater.Rotation = FRotator(Random.FRandRange(0.0f, 360.0f), Random.FRandRange(0.0f, 360.0f), Random.FRandRange(0.0f, 360.0f));
	Floater.Kind = Random.RandRange(0, NumFloaterKinds - 1);
}

// ---------------------------------------------------------------------------------------------
// Frame update

void AAmplitudeStage::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (TrackMesh == nullptr)
	{
		return;
	}
	const float Dt = FMath::Clamp(DeltaSeconds, 0.0f, 0.1f);
	UpdateTiming(Dt);
	UpdatePalette(Dt);
	UpdateLaneColors();
	UpdateShip(Dt);
	UpdateCamera(Dt);
	UpdateTrackMesh();
	UpdateHoops();
	UpdateGems();
	UpdatePowerups();
	UpdateTargets(Dt);
	UpdateSparks(Dt);
	UpdateFloaters(Dt);
	UpdateSky();
	UpdatePostProcess();
}

const FAmplitudeSession* AAmplitudeStage::GetSession() const
{
	const AAmplitudeDirector* OwningDirector = Director.Get();
	return OwningDirector != nullptr ? OwningDirector->GetSession() : nullptr;
}

int32 AAmplitudeStage::GetDisplayedShipLane() const
{
	const FAmplitudeSession* Session = GetSession();
	return Session != nullptr ? Session->GetSimulation().GetShipLane() : AttractShipLane;
}

void AAmplitudeStage::UpdateTiming(float DeltaSeconds)
{
	const FAmplitudeSession* Session = GetSession();
	const bool bHadSession = bHasSession;
	bHasSession = Session != nullptr;
	PreviousSongTimeMs = SongTimeMs;
	VisualSeconds += DeltaSeconds;

	if (Session != nullptr)
	{
		const FAmplitudeSongDefinition& Song = Session->GetSong();
		SongTimeMs = Session->GetSongTimeMs();
		ApproachMs = FMath::Max(200.0, Session->GetSimulation().GetParams().ApproachTimeMs);
		BeatMs = 60000.0 / FMath::Max(1.0, Song.Bpm);
		FirstBeatMs = Song.GetFirstBeatMs();
	}
	else
	{
		AttractTimeMs += DeltaSeconds * 1000.0;
		SongTimeMs = AttractTimeMs;
		ApproachMs = 2200.0;
		BeatMs = 60000.0 / 116.0;
		FirstBeatMs = 0.0;
	}
	if (bHadSession != bHasSession || SongTimeMs < PreviousSongTimeMs - 1.0 || SongTimeMs > PreviousSongTimeMs + 1000.0)
	{
		// New session, back to the menus or a restart: nothing "crossed" the hit line in between.
		PreviousSongTimeMs = SongTimeMs;
	}

	const double Beats = (SongTimeMs - FirstBeatMs) / BeatMs;
	BeatPulse = static_cast<float>(FMath::Exp(-(Beats - FMath::FloorToDouble(Beats)) * 5.0));

	float Audible = 1.0f;
	if (Session != nullptr)
	{
		Audible = 0.0f;
		for (int32 Lane = 0; Lane < Amp::NumLanes; ++Lane)
		{
			Audible += Session->GetSimulation().GetLaneMixGain(Lane) / Amp::NumLanes;
		}
	}
	MusicLevel = FMath::Lerp(MusicLevel, Audible, 1.0f - FMath::Exp(-DeltaSeconds * 4.0f));
	if (!bHasSession)
	{
		AttractShipLane = static_cast<int32>(HashInts(FMath::FloorToInt64(Beats / 8.0), 17) % Amp::NumLanes);
	}

	// The road winds with the music: slow, overlapping sways across and up.
	const double Seconds = SongTimeMs / 1000.0;
	BendAcross = 2.4e-5 * FMath::Sin(Seconds * 0.23) + 0.9e-5 * FMath::Sin(Seconds * 0.61 + 1.3);
	BendUp = 0.9e-5 + 0.6e-5 * FMath::Sin(Seconds * 0.19 + 0.4);
}

void AAmplitudeStage::UpdatePalette(float DeltaSeconds)
{
	if (bHasSession)
	{
		// A new look every eight bars.
		const double SectionMs = BeatMs * 32.0;
		PaletteIndex = static_cast<int32>(FMath::FloorToDouble(FMath::Max(0.0, SongTimeMs - FirstBeatMs) / SectionMs)) % NumPalettes;
	}
	else
	{
		PaletteIndex = static_cast<int32>(VisualSeconds / 14.0) % NumPalettes;
	}
	Palette = FAmplitudeStagePalette::Lerp(Palette, GetPalette(PaletteIndex), 1.0f - FMath::Exp(-DeltaSeconds * 0.9f));

	Fog->SetFogInscatteringColor(Palette.Horizon);
	SunLight->SetLightColor(FMath::Lerp(FLinearColor::White, Palette.Sun, 0.6f));
	FillLight->SetLightColor(FMath::Lerp(FLinearColor::White, Palette.SkyMid, 0.7f));
	SunMaterial->SetVectorParameterValue(ColorParam, Palette.Sun);
	// The world lights up with the music: the more instruments are playing, the stronger the pulse.
	HoopMaterial->SetScalarParameterValue(GlowParam, 0.5f + 1.6f * BeatPulse * MusicLevel);
	for (int32 Kind = 0; Kind < FloaterMaterials.Num(); ++Kind)
	{
		FloaterMaterials[Kind]->SetVectorParameterValue(ColorParam, Palette.Shapes[Kind % 3]);
		FloaterMaterials[Kind]->SetScalarParameterValue(GlowParam, 0.05f + 0.6f * BeatPulse * MusicLevel);
	}

	if (VisualSeconds >= NextSkyCaptureSeconds)
	{
		// Keeps ambient light and reflections in step with the sky colours.
		SkyLight->RecaptureSky();
		NextSkyCaptureSeconds = VisualSeconds + 3.0;
	}
}

void AAmplitudeStage::UpdateLaneColors()
{
	for (int32 Lane = 0; Lane < Amp::NumLanes; ++Lane)
	{
		const FLinearColor Color = AmplitudeStyle::GetLaneColor(Lane);
		if (Color.Equals(LaneColors[Lane]))
		{
			continue;
		}
		LaneColors[Lane] = Color;
		GemMaterials[Lane]->SetVectorParameterValue(ColorParam, Color);
		TargetMaterials[Lane]->SetVectorParameterValue(ColorParam, Color);
		SparkMaterials[Lane]->SetVectorParameterValue(ColorParam, Color);
	}
}

void AAmplitudeStage::UpdateShip(float DeltaSeconds)
{
	const FAmplitudeSession* Session = GetSession();
	const int32 Lane = GetDisplayedShipLane();

	const double Previous = ShipAcross;
	ShipAcross = FMath::Lerp(ShipAcross, LaneCenter(Lane), 1.0 - FMath::Exp(-DeltaSeconds * 20.0));
	ShipAcrossVelocity = DeltaSeconds > 0.0f ? (ShipAcross - Previous) / DeltaSeconds : 0.0;

	const FTrackFrame Frame = GetFrame(-30.0, ShipAcross);
	ShipLocation = Frame.Position + Frame.Normal * (30.0 + 3.0 * FMath::Sin(VisualSeconds * 3.0));
	const float Bank = FMath::Clamp(static_cast<float>(-ShipAcrossVelocity * 0.012), -28.0f, 28.0f);
	const FQuat Rotation = FRotationMatrix::MakeFromXZ(Frame.Forward, Frame.Normal).ToQuat() * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(Bank));
	ShipRoot->SetWorldLocationAndRotation(ShipLocation, Rotation);

	const FLinearColor Accent = LaneColors[Lane];
	ShipAccentMaterial->SetVectorParameterValue(ColorParam, Accent);
	ShipGlowMaterial->SetVectorParameterValue(ColorParam, Accent);
	ShipGlowMaterial->SetScalarParameterValue(GlowParam, 0.6f + 0.8f * BeatPulse);
	BeamMaterial->SetVectorParameterValue(ColorParam, Accent);

	for (int32 Column = 0; Column < Amp::NumColumns; ++Column)
	{
		BeamFlash[Column] = FMath::Max(0.0f, BeamFlash[Column] - DeltaSeconds * 7.0f);
		const float Flash = BeamFlash[Column];
		Beams[Column]->SetVisibility(Flash > 0.01f);
		if (Flash > 0.01f)
		{
			Beams[Column]->SetRelativeScale3D(FVector(0.13 * Flash, 0.13 * Flash, 2.4));
		}
		ShipEmitters[Column]->SetRelativeScale3D(FVector(0.2, 0.2, 0.5) * (1.0 + 0.6 * Flash));
	}

	const bool bShield = Session != nullptr && Session->GetSimulation().IsShieldActive();
	ShieldBubble->SetVisibility(bShield);
	if (bShield)
	{
		const double Pulse = 1.0 + 0.05 * FMath::Sin(VisualSeconds * 6.0);
		ShieldBubble->SetRelativeScale3D(FVector(3.0, 3.0, 1.3) * Pulse);
	}
}

void AAmplitudeStage::UpdateCamera(float DeltaSeconds)
{
	CameraAcross = FMath::Lerp(CameraAcross, ShipAcross * 0.82, 1.0 - FMath::Exp(-DeltaSeconds * 7.0));
	const FTrackFrame Base = GetFrame(-1150.0, CameraAcross);
	FVector Location = Base.Position + Base.Normal * 440.0;
	const FVector LookAt = TrackPoint(2300.0, CameraAcross * 0.9, 0.0);

	ShakeAmount *= FMath::Exp(-DeltaSeconds * 12.0f);
	if (ShakeAmount > 0.05f)
	{
		Location += FVector(Random.FRandRange(-1.0f, 1.0f), Random.FRandRange(-1.0f, 1.0f), Random.FRandRange(-1.0f, 1.0f)) * ShakeAmount;
	}

	FRotator Rotation = (LookAt - Location).Rotation();
	Rotation.Roll = static_cast<float>(-FMath::RadiansToDegrees(CameraAcross / ArcRadius) * 0.5);
	Camera->SetWorldLocationAndRotation(Location, Rotation);

	FovKick = FMath::Max(0.0f, FovKick - DeltaSeconds * 3.0f);
	const FAmplitudeSession* Session = GetSession();
	const bool bFever = Session != nullptr && Session->GetSimulation().IsFeverActive();
	Camera->SetFieldOfView(72.0f + 3.0f * FovKick + (bFever ? 2.0f : 0.0f));
}

void AAmplitudeStage::UpdateTrackMesh()
{
	const FAmplitudeSession* Session = GetSession();
	const Amp::FSimulation* Simulation = Session != nullptr ? &Session->GetSimulation() : nullptr;
	const int32 ShipLane = GetDisplayedShipLane();
	const FLinearColor Muted(0.32f, 0.33f, 0.36f);

	TrackSurface.Reset();
	TrackGlow.Reset();
	TrackBeats.Reset();
	TrackHitLine.Reset();

	FLinearColor LaneTints[Amp::NumLanes];
	for (int32 Lane = 0; Lane < Amp::NumLanes; ++Lane)
	{
		const bool bShipLane = Lane == ShipLane;
		const bool bCaptured = Simulation != nullptr && Simulation->GetLane(Lane).bCaptured;
		const bool bMuted = Simulation != nullptr && Simulation->GetLane(Lane).bMuted;
		// Lanes whose instrument is playing (live or captured) light up; the rest stay dark.
		const bool bAudible = Simulation == nullptr || Simulation->GetLaneMixGain(Lane) > 0.5f;
		const FLinearColor LaneColor = LaneColors[Lane];
		FLinearColor Tint = FMath::Lerp(Palette.Track, LaneColor, (bShipLane ? 0.22f : 0.08f) + (bAudible ? 0.08f : 0.0f) + 0.2f * LaneFlash[Lane]);
		if (bMuted)
		{
			Tint = FMath::Lerp(Tint, Muted, 0.6f);
		}
		Tint = FMath::Lerp(Tint, AmplitudeStyle::Miss, 0.35f * MissFlash[Lane]);
		LaneTints[Lane] = Tint;

		const double Center = LaneCenter(Lane);
		const double Across[3] = {Center - LaneWidth * 0.5, Center, Center + LaneWidth * 0.5};
		const double EdgeWidth = bShipLane ? 6.0 : 3.5;
		const FLinearColor EdgeColor = (bShipLane || bAudible) ? LaneColor : LaneColor * 0.3f;

		// Surface (3 samples across so the half-pipe curve shows) and the two glowing edges.
		const int32 SurfaceStart = TrackSurface.Vertices.Num();
		const int32 GlowStart = TrackGlow.Vertices.Num();
		for (int32 Step = 0; Step <= TrackSegments; ++Step)
		{
			const double Distance = -BehindLength + (TrackLength + BehindLength) * Step / TrackSegments;
			const float Far = FMath::SmoothStep(0.55f, 1.0f, static_cast<float>(Distance / TrackLength));

			FLinearColor RowColor = Tint;
			if (bCaptured)
			{
				// A captured lane plays itself: colour flows down it.
				const float Flow = 0.5f + 0.5f * FMath::Sin(static_cast<float>(Distance * 0.006 - VisualSeconds * 9.0));
				RowColor = FMath::Lerp(Palette.Track, LaneColor, 0.3f + 0.2f * Flow + 0.2f * CaptureFlash[Lane]);
			}
			RowColor = FMath::Lerp(RowColor, Palette.Horizon, Far);

			for (int32 Sample = 0; Sample < 3; ++Sample)
			{
				const FTrackFrame Frame = GetFrame(Distance, Across[Sample]);
				TrackSurface.Vertices.Add(Frame.Position);
				TrackSurface.Normals.Add(Frame.Normal);
				TrackSurface.Colors.Add(Sample == 1 ? RowColor : RowColor * 0.93f);
			}

			const FLinearColor RowEdge = FMath::Lerp(EdgeColor, Palette.Horizon, Far);
			for (const double Edge : {Across[0], Across[2]})
			{
				for (const double Offset : {-EdgeWidth, EdgeWidth})
				{
					const FTrackFrame Frame = GetFrame(Distance, Edge + Offset);
					TrackGlow.Vertices.Add(Frame.Position + Frame.Normal * 1.2);
					TrackGlow.Normals.Add(Frame.Normal);
					TrackGlow.Colors.Add(RowEdge);
				}
			}
		}
		for (int32 Step = 0; Step < TrackSegments; ++Step)
		{
			for (int32 Sample = 0; Sample < 2; ++Sample)
			{
				const int32 A = SurfaceStart + Step * 3 + Sample;
				TrackSurface.AddQuad(A, A + 1, A + 3, A + 4, bDoubleSidedGeometry);
			}
			for (int32 EdgeIndex = 0; EdgeIndex < 2; ++EdgeIndex)
			{
				const int32 A = GlowStart + Step * 4 + EdgeIndex * 2;
				TrackGlow.AddQuad(A, A + 1, A + 4, A + 5, bDoubleSidedGeometry);
			}
		}

		// Hit line across the lane.
		const FLinearColor HitColor = bShipLane ? FMath::Lerp(LaneColor, FLinearColor::White, 0.35f) : FLinearColor(0.22f, 0.23f, 0.28f);
		const int32 HitStart = TrackHitLine.Vertices.Num();
		for (const double Distance : {-7.0, 7.0})
		{
			for (int32 Sample = 0; Sample < 3; ++Sample)
			{
				const FTrackFrame Frame = GetFrame(Distance, Across[Sample]);
				TrackHitLine.Vertices.Add(Frame.Position + Frame.Normal * 1.6);
				TrackHitLine.Normals.Add(Frame.Normal);
				TrackHitLine.Colors.Add(HitColor);
			}
		}
		for (int32 Sample = 0; Sample < 2; ++Sample)
		{
			const int32 A = HitStart + Sample;
			TrackHitLine.AddQuad(A, A + 1, A + 3, A + 4, bDoubleSidedGeometry);
		}
	}

	// Beat lines scroll down every lane; bar lines are thicker.
	const double BehindMs = BehindLength / TrackLength * ApproachMs;
	const int64 FirstBeat = FMath::CeilToInt64((SongTimeMs - BehindMs - FirstBeatMs) / BeatMs);
	const int64 LastBeat = FMath::FloorToInt64((SongTimeMs + ApproachMs - FirstBeatMs) / BeatMs);
	for (int64 Beat = FirstBeat; Beat <= LastBeat; ++Beat)
	{
		const double Distance = DistanceForTime(FirstBeatMs + Beat * BeatMs);
		const bool bBar = ((Beat % 4) + 4) % 4 == 0;
		const double HalfThickness = bBar ? 9.0 : 4.0;
		const float Far = FMath::SmoothStep(0.55f, 1.0f, static_cast<float>(Distance / TrackLength));
		for (int32 Lane = 0; Lane < Amp::NumLanes; ++Lane)
		{
			const double Center = LaneCenter(Lane);
			const double Across[3] = {Center - LaneWidth * 0.5 + 6.0, Center, Center + LaneWidth * 0.5 - 6.0};
			const FLinearColor Color = FMath::Lerp(FMath::Lerp(LaneTints[Lane], FLinearColor::White, bBar ? 0.22f : 0.1f), Palette.Horizon, Far);
			const int32 Start = TrackBeats.Vertices.Num();
			for (const double Offset : {-HalfThickness, HalfThickness})
			{
				for (int32 Sample = 0; Sample < 3; ++Sample)
				{
					const FTrackFrame Frame = GetFrame(Distance + Offset, Across[Sample]);
					TrackBeats.Vertices.Add(Frame.Position + Frame.Normal * 0.9);
					TrackBeats.Normals.Add(Frame.Normal);
					TrackBeats.Colors.Add(Color);
				}
			}
			for (int32 Sample = 0; Sample < 2; ++Sample)
			{
				const int32 A = Start + Sample;
				TrackBeats.AddQuad(A, A + 1, A + 3, A + 4, bDoubleSidedGeometry);
			}
		}
	}

	TrackSurface.Upload(TrackMesh, 0);
	TrackGlow.Upload(TrackMesh, 1);
	TrackBeats.Upload(TrackMesh, 2);
	TrackHitLine.Upload(TrackMesh, 3);
}

void AAmplitudeStage::UpdateHoops()
{
	constexpr int32 MajorSegments = 48;
	constexpr int32 MinorSegments = 8;
	Hoops.Reset();

	const double BarMs = BeatMs * 4.0;
	const double BehindMs = BehindLength / TrackLength * ApproachMs;
	const int64 FirstBar = FMath::CeilToInt64((SongTimeMs - BehindMs * 1.3 - FirstBeatMs) / BarMs);
	const int64 LastBar = FMath::FloorToInt64((SongTimeMs + ApproachMs * 1.05 - FirstBeatMs) / BarMs);
	for (int64 Bar = FirstBar; Bar <= LastBar; ++Bar)
	{
		const double Distance = DistanceForTime(FirstBeatMs + Bar * BarMs);
		const FTrackFrame Frame = GetFrame(Distance, 0.0);
		const FVector Center = Frame.Position + Frame.Normal * 150.0;
		const bool bPhrase = ((Bar % 4) + 4) % 4 == 0;
		const double Minor = bPhrase ? 34.0 : 20.0;
		// Hoops flare as they pass the hit line (the downbeat).
		const float Flash = FMath::Clamp(1.0f - static_cast<float>(FMath::Abs(Distance + 150.0) / 450.0), 0.0f, 1.0f);
		const float Far = FMath::SmoothStep(0.6f, 1.05f, static_cast<float>(Distance / TrackLength));
		const FLinearColor Color = FMath::Lerp(FMath::Lerp(Palette.Hoop, LaneColors[GetDisplayedShipLane()], 0.8f * Flash), Palette.Horizon, Far);

		const int32 Start = Hoops.Vertices.Num();
		for (int32 Major = 0; Major <= MajorSegments; ++Major)
		{
			const double Angle = UE_DOUBLE_TWO_PI * Major / MajorSegments;
			const FVector Radial = Frame.Lateral * FMath::Cos(Angle) + Frame.Normal * FMath::Sin(Angle);
			for (int32 MinorIndex = 0; MinorIndex <= MinorSegments; ++MinorIndex)
			{
				const double Tube = UE_DOUBLE_TWO_PI * MinorIndex / MinorSegments;
				const FVector Normal = Radial * FMath::Cos(Tube) + Frame.Forward * FMath::Sin(Tube);
				Hoops.Vertices.Add(Center + Radial * HoopRadius + Normal * Minor);
				Hoops.Normals.Add(Normal);
				Hoops.Colors.Add(Color);
			}
		}
		for (int32 Major = 0; Major < MajorSegments; ++Major)
		{
			for (int32 MinorIndex = 0; MinorIndex < MinorSegments; ++MinorIndex)
			{
				const int32 A = Start + Major * (MinorSegments + 1) + MinorIndex;
				const int32 C = A + MinorSegments + 1;
				Hoops.AddQuad(A, A + 1, C, C + 1, bDoubleSidedGeometry);
			}
		}
	}
	Hoops.Upload(HoopMesh, 0);
}

void AAmplitudeStage::CollectAttractGems(double FromMs, double ToMs, TArray<FAttractGem>& Out) const
{
	// A made-up chart for the menus: eighth notes, denser on the beat, lanes taking turns.
	Out.Reset();
	const double EighthMs = BeatMs * 0.5;
	const int64 First = FMath::CeilToInt64(FromMs / EighthMs);
	const int64 Last = FMath::FloorToInt64(ToMs / EighthMs);
	for (int64 Eighth = First; Eighth <= Last; ++Eighth)
	{
		const int64 Phrase = FMath::FloorToInt64(static_cast<double>(Eighth) / 16.0);
		for (int32 Lane = 0; Lane < Amp::NumLanes; ++Lane)
		{
			if (HashInts(Phrase, Lane + 101) % 3 == 0)
			{
				continue;
			}
			const uint32 Hash = HashInts(Eighth, Lane * 7 + 3);
			const uint32 Chance = (Eighth % 2 == 0) ? 55u : 18u;
			if (Hash % 100u >= Chance)
			{
				continue;
			}
			Out.Add({Eighth * EighthMs, Lane, static_cast<int32>((Hash / 100u) % 3u)});
		}
	}
}

void AAmplitudeStage::UpdateGems()
{
	TArray<FTransform> PerLane[Amp::NumLanes];
	TArray<FTransform> Faded;
	const FAmplitudeSession* Session = GetSession();
	const int32 ShipLane = GetDisplayedShipLane();
	const double BehindMs = BehindLength / TrackLength * ApproachMs;
	const double FromMs = SongTimeMs - BehindMs;
	const double ToMs = SongTimeMs + ApproachMs;
	const FVector GemScale(0.5, 0.46, 0.26);

	auto SpawnScale = [](double Distance)
	{
		// Gems grow in at the far end instead of popping.
		return static_cast<float>(FMath::Clamp((TrackLength - Distance) / (TrackLength * 0.07), 0.0, 1.0));
	};

	if (Session != nullptr)
	{
		const Amp::FSimulation& Simulation = Session->GetSimulation();
		const std::vector<Amp::FNote>& Notes = Simulation.GetNotes();
		auto It = std::lower_bound(Notes.begin(), Notes.end(), FromMs, [](const Amp::FNote& Note, double TimeMs) { return Note.TimeMs < TimeMs; });
		for (; It != Notes.end() && It->TimeMs <= ToMs; ++It)
		{
			const Amp::FNote& Note = *It;
			const bool bHit = Note.Judgement == Amp::EJudgement::Perfect || Note.Judgement == Amp::EJudgement::Good;
			if (Note.bCleared || Note.bAutoPlayed || bHit || Note.Lane < 0 || Note.Lane >= Amp::NumLanes)
			{
				continue;
			}
			if (Simulation.GetLane(Note.Lane).bCaptured && Note.IsPending())
			{
				// Captured lanes play themselves; their gems are replaced by the flowing lane colour.
				continue;
			}
			const double Distance = DistanceForTime(Note.TimeMs);
			const double Across = LaneCenter(Note.Lane) + ColumnOffset(Note.Column);
			float Scale = SpawnScale(Distance);
			if (Note.Judgement == Amp::EJudgement::Miss)
			{
				Faded.Add(MakeTrackTransform(Distance, Across, GemHeight * 0.6, GemScale * 0.8f * Scale));
				continue;
			}
			if (Note.Lane == ShipLane)
			{
				Scale *= 1.0f + 0.1f * BeatPulse;
			}
			PerLane[Note.Lane].Add(MakeTrackTransform(Distance, Across, GemHeight, GemScale * Scale));
		}
	}
	else
	{
		CollectAttractGems(FromMs, ToMs, AttractScratch);
		for (const FAttractGem& Gem : AttractScratch)
		{
			const double Across = LaneCenter(Gem.Lane) + ColumnOffset(Gem.Column);
			if (Gem.Lane == AttractShipLane && Gem.TimeMs <= SongTimeMs)
			{
				if (Gem.TimeMs > PreviousSongTimeMs)
				{
					// The attract-mode Beat Blaster never misses.
					SpawnSparks(TrackPoint(0.0, Across, GemHeight), Gem.Lane, 10, 700.0f, 0.09f, 0.75f);
					TargetFlash[Gem.Lane][Gem.Column] = 1.0f;
					BeamFlash[Gem.Column] = 1.0f;
				}
				continue;
			}
			const double Distance = DistanceForTime(Gem.TimeMs);
			PerLane[Gem.Lane].Add(MakeTrackTransform(Distance, Across, GemHeight, GemScale * SpawnScale(Distance)));
		}
	}

	for (int32 Lane = 0; Lane < Amp::NumLanes; ++Lane)
	{
		SetInstances(GemInstances[Lane], PerLane[Lane]);
	}
	SetInstances(FadedGemInstances, Faded);
}

void AAmplitudeStage::UpdatePowerups()
{
	TArray<FTransform> PerType[Amp::NumPowerupTypes];
	if (const FAmplitudeSession* Session = GetSession())
	{
		// Powerups tumble down their lane like a gem the size of a fist, standing on a corner.
		const FQuat Tilt(FRotator(35.26f, 0.0f, 45.0f));
		for (const Amp::FTrackPowerup& Powerup : Session->GetSimulation().GetPowerups())
		{
			const double Distance = DistanceForTime(Powerup.ArrivalMs);
			if (Distance < -BehindLength || Distance > TrackLength)
			{
				continue;
			}
			const float Spin = static_cast<float>(VisualSeconds * 140.0 + Powerup.Id * 37.0);
			const double Height = 62.0 + 8.0 * FMath::Sin(VisualSeconds * 4.0 + Powerup.Id);
			FTransform Transform = MakeTrackTransform(Distance, LaneCenter(Powerup.Lane), Height, FVector(0.46), Spin);
			Transform.SetRotation(Transform.GetRotation() * Tilt);
			PerType[PowerupSlot(Powerup.Type) - FirstPowerupSlot].Add(Transform);
		}
	}
	for (int32 Type = 0; Type < Amp::NumPowerupTypes; ++Type)
	{
		SetInstances(PowerupInstances[Type], PerType[Type]);
	}
}

void AAmplitudeStage::UpdateTargets(float DeltaSeconds)
{
	const int32 ShipLane = GetDisplayedShipLane();
	TArray<FTransform> Transforms;
	for (int32 Lane = 0; Lane < Amp::NumLanes; ++Lane)
	{
		const bool bShipLane = Lane == ShipLane;
		Transforms.Reset();
		float Brightest = 0.0f;
		for (int32 Column = 0; Column < Amp::NumColumns; ++Column)
		{
			float& Flash = TargetFlash[Lane][Column];
			Flash = FMath::Max(0.0f, Flash - DeltaSeconds * 6.0f);
			Brightest = FMath::Max(Brightest, Flash);
			const double Scale = (bShipLane ? 0.66 : 0.5) * (1.0 + 0.45 * Flash);
			Transforms.Add(MakeTrackTransform(0.0, LaneCenter(Lane) + ColumnOffset(Column), 2.5, FVector(Scale, Scale, 0.035)));
		}
		SetInstances(TargetInstances[Lane], Transforms);
		TargetMaterials[Lane]->SetScalarParameterValue(GlowParam, (bShipLane ? 1.2f + 0.4f * BeatPulse : 0.2f) + 1.8f * Brightest);

		LaneFlash[Lane] = FMath::Max(0.0f, LaneFlash[Lane] - DeltaSeconds * 4.0f);
		MissFlash[Lane] = FMath::Max(0.0f, MissFlash[Lane] - DeltaSeconds * 3.0f);
		CaptureFlash[Lane] = FMath::Max(0.0f, CaptureFlash[Lane] - DeltaSeconds * 1.2f);
	}
}

void AAmplitudeStage::UpdateSparks(float DeltaSeconds)
{
	TArray<FTransform> PerSlot[NumSparkSlots];
	const float Drag = FMath::Exp(-DeltaSeconds * 2.2f);
	for (int32 Index = Sparks.Num() - 1; Index >= 0; --Index)
	{
		FSpark& Spark = Sparks[Index];
		Spark.Age += DeltaSeconds;
		if (Spark.Age >= Spark.Life)
		{
			Sparks.RemoveAtSwap(Index);
			continue;
		}
		Spark.Velocity *= Drag;
		Spark.Velocity.Z += Spark.Gravity * DeltaSeconds;
		Spark.Position += Spark.Velocity * DeltaSeconds;
		const float Scale = Spark.Size * (1.0f - Spark.Age / Spark.Life);
		PerSlot[FMath::Clamp(Spark.Slot, 0, NumSparkSlots - 1)].Add(FTransform(FQuat::Identity, Spark.Position, FVector(Scale)));
	}
	for (int32 Slot = 0; Slot < NumSparkSlots; ++Slot)
	{
		SetInstances(SparkInstances[Slot], PerSlot[Slot]);
	}
}

void AAmplitudeStage::UpdateFloaters(float DeltaSeconds)
{
	// Scenery drifts past at a third of the track speed, following song time (so it stops on pause).
	const double SongDeltaMs = FMath::Clamp(SongTimeMs - PreviousSongTimeMs, 0.0, 200.0);
	const double Move = SongDeltaMs / ApproachMs * TrackLength * 0.35;

	TArray<FTransform> PerKind[NumFloaterKinds];
	for (FFloater& Floater : Floaters)
	{
		Floater.Position.X -= Move;
		Floater.Rotation.Yaw += Floater.Spin * DeltaSeconds;
		Floater.Rotation.Pitch += Floater.Spin * 0.5f * DeltaSeconds;
		if (Floater.Position.X < FloaterNear)
		{
			RespawnFloater(Floater, false);
		}
		const FVector Location = Floater.Position + FVector(0.0, 0.0, 90.0 * FMath::Sin(VisualSeconds * 0.6 + Floater.Phase));
		const double Scale = Floater.Radius / 50.0 * (1.0 + 0.08 * BeatPulse * MusicLevel);
		PerKind[Floater.Kind].Add(FTransform(Floater.Rotation, Location, FVector(Scale)));
	}
	for (int32 Kind = 0; Kind < NumFloaterKinds; ++Kind)
	{
		SetInstances(FloaterInstances[Kind], PerKind[Kind]);
	}
}

void AAmplitudeStage::UpdateSky()
{
	TArray<FLinearColor> Colors;
	Colors.Reserve(SkyElevation.Num());
	for (const float Elevation : SkyElevation)
	{
		FLinearColor Color;
		if (Elevation < 0.0f)
		{
			Color = FMath::Lerp(Palette.Horizon, Palette.Ground, FMath::SmoothStep(0.0f, 0.3f, -Elevation));
		}
		else if (Elevation < 0.22f)
		{
			Color = FMath::Lerp(Palette.Horizon, Palette.SkyMid, FMath::SmoothStep(0.0f, 0.22f, Elevation));
		}
		else
		{
			Color = FMath::Lerp(Palette.SkyMid, Palette.SkyTop, FMath::SmoothStep(0.22f, 0.85f, Elevation));
		}
		Colors.Add(Color);
	}
	SkyMesh->UpdateMeshSection_LinearColor(0, SkyVertices, SkyNormals, TArray<FVector2D>(), Colors, TArray<FProcMeshTangent>());
}

void AAmplitudeStage::UpdatePostProcess()
{
	const FAmplitudeSession* Session = GetSession();
	const bool bSlowMotion = Session != nullptr && Session->GetSimulation().IsSlowMotionActive();
	const bool bLowEnergy = Session != nullptr && Session->GetSimulation().IsEnergyLow() && !Session->GetSimulation().IsFinished();

	FPostProcessSettings& Settings = Camera->PostProcessSettings;
	Settings.bOverride_AutoExposureMethod = true;
	Settings.AutoExposureMethod = AEM_Manual;
	Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Settings.AutoExposureApplyPhysicalCameraExposure = 0;
	Settings.bOverride_AutoExposureBias = true;
	Settings.AutoExposureBias = CVarStageExposure.GetValueOnGameThread();
	Settings.bOverride_BloomIntensity = true;
	Settings.BloomIntensity = 0.9f;
	Settings.bOverride_VignetteIntensity = true;
	Settings.VignetteIntensity = 0.45f;
	Settings.bOverride_MotionBlurAmount = true;
	Settings.MotionBlurAmount = 0.0f;
	Settings.bOverride_LensFlareIntensity = true;
	Settings.LensFlareIntensity = 0.0f;

	// Slow Motion drains 30% of the colour (spec 12.6.2); low energy pulses a red tint.
	const double Saturation = bSlowMotion ? 0.7 : 1.0;
	Settings.bOverride_ColorSaturation = true;
	Settings.ColorSaturation = FVector4(Saturation, Saturation, Saturation, 1.0);
	const float Alarm = bLowEnergy ? 0.5f + 0.5f * FMath::Sin(static_cast<float>(VisualSeconds) * 8.0f) : 0.0f;
	Settings.bOverride_SceneColorTint = true;
	Settings.SceneColorTint = FMath::Lerp(FLinearColor::White, FLinearColor(1.0f, 0.78f, 0.78f), Alarm * 0.6f);
}

// ---------------------------------------------------------------------------------------------
// Track geometry

AAmplitudeStage::FTrackFrame AAmplitudeStage::GetFrame(double Distance, double Across) const
{
	const FVector Center(Distance, BendAcross * Distance * Distance, BendUp * Distance * Distance);
	const FVector Forward = FVector(1.0, 2.0 * BendAcross * Distance, 2.0 * BendUp * Distance).GetSafeNormal();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
	const FVector Up = FVector::CrossProduct(Forward, Right);
	const double Angle = Across / ArcRadius;
	const double Sin = FMath::Sin(Angle);
	const double Cos = FMath::Cos(Angle);

	FTrackFrame Frame;
	Frame.Position = Center + Right * (ArcRadius * Sin) + Up * (ArcRadius * (1.0 - Cos));
	Frame.Forward = Forward;
	Frame.Lateral = Right * Cos + Up * Sin;
	Frame.Normal = Up * Cos - Right * Sin;
	return Frame;
}

FVector AAmplitudeStage::TrackPoint(double Distance, double Across, double Height) const
{
	const FTrackFrame Frame = GetFrame(Distance, Across);
	return Frame.Position + Frame.Normal * Height;
}

FTransform AAmplitudeStage::MakeTrackTransform(double Distance, double Across, double Height, const FVector& Scale, float SpinDegrees) const
{
	const FTrackFrame Frame = GetFrame(Distance, Across);
	FQuat Rotation = FRotationMatrix::MakeFromXZ(Frame.Forward, Frame.Normal).ToQuat();
	if (SpinDegrees != 0.0f)
	{
		Rotation = Rotation * FQuat(FVector::UpVector, FMath::DegreesToRadians(SpinDegrees));
	}
	return FTransform(Rotation, Frame.Position + Frame.Normal * Height, Scale);
}

double AAmplitudeStage::DistanceForTime(double TimeMs) const
{
	return (TimeMs - SongTimeMs) / ApproachMs * TrackLength;
}

double AAmplitudeStage::LaneCenter(int32 Lane)
{
	return (static_cast<double>(Lane) - (Amp::NumLanes - 1) * 0.5) * (LaneWidth + LaneGap);
}

double AAmplitudeStage::ColumnOffset(int32 Column)
{
	return static_cast<double>(FMath::Clamp(Column, 0, Amp::NumColumns - 1) - 1) * ColumnSpacing;
}

FVector AAmplitudeStage::GetHitPoint(int32 Lane, int32 Column) const
{
	return TrackPoint(0.0, LaneCenter(FMath::Clamp(Lane, 0, Amp::NumLanes - 1)) + ColumnOffset(Column), GemHeight);
}

bool AAmplitudeStage::ProjectToViewport(const FVector& WorldLocation, FVector2D& OutFraction) const
{
	const UWorld* World = GetWorld();
	const APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Controller == nullptr)
	{
		return false;
	}
	FVector2D ScreenLocation;
	if (!Controller->ProjectWorldLocationToScreen(WorldLocation, ScreenLocation, false))
	{
		return false;
	}
	int32 SizeX = 0;
	int32 SizeY = 0;
	Controller->GetViewportSize(SizeX, SizeY);
	if (SizeX <= 0 || SizeY <= 0)
	{
		return false;
	}
	OutFraction = FVector2D(ScreenLocation.X / SizeX, ScreenLocation.Y / SizeY);
	return true;
}

// ---------------------------------------------------------------------------------------------
// Effects

int32 AAmplitudeStage::PowerupSlot(Amp::EPowerupType Type)
{
	return FirstPowerupSlot + FMath::Clamp(static_cast<int32>(Type), 0, Amp::NumPowerupTypes - 1);
}

void AAmplitudeStage::SpawnSparks(const FVector& Location, int32 Slot, int32 Count, float Speed, float Size, float UpBias)
{
	// Sparks are left behind as the track rushes towards the camera.
	const FVector Drift(-TrackLength / ApproachMs * 1000.0 * 0.25, 0.0, 0.0);
	for (int32 Index = 0; Index < Count && Sparks.Num() < MaxSparks; ++Index)
	{
		FVector Direction = Random.GetUnitVector();
		Direction.Z = FMath::Lerp(Direction.Z, FMath::Abs(Direction.Z), UpBias);
		FSpark Spark;
		Spark.Position = Location;
		Spark.Velocity = Direction.GetSafeNormal() * Speed * Random.FRandRange(0.45f, 1.0f) + Drift;
		Spark.Size = Size * Random.FRandRange(0.6f, 1.2f);
		Spark.Life = Random.FRandRange(0.35f, 0.7f);
		Spark.Gravity = -1400.0f;
		Spark.Slot = Slot;
		Sparks.Add(Spark);
	}
}

void AAmplitudeStage::AddShake(float Amount)
{
	ShakeAmount = FMath::Max(ShakeAmount, Amount);
}

void AAmplitudeStage::SetInstances(UInstancedStaticMeshComponent* Component, const TArray<FTransform>& Transforms)
{
	if (Component == nullptr)
	{
		return;
	}
	if (Component->GetInstanceCount() != Transforms.Num())
	{
		Component->ClearInstances();
		if (Transforms.Num() > 0)
		{
			Component->AddInstances(Transforms, false);
		}
		return;
	}
	if (Transforms.Num() > 0)
	{
		Component->BatchUpdateInstancesTransforms(0, Transforms, false, true, true);
	}
}

void AAmplitudeStage::ResetEffects()
{
	for (int32 Lane = 0; Lane < Amp::NumLanes; ++Lane)
	{
		for (int32 Column = 0; Column < Amp::NumColumns; ++Column)
		{
			TargetFlash[Lane][Column] = 0.0f;
		}
		LaneFlash[Lane] = 0.0f;
		MissFlash[Lane] = 0.0f;
		CaptureFlash[Lane] = 0.0f;
	}
	for (int32 Column = 0; Column < Amp::NumColumns; ++Column)
	{
		BeamFlash[Column] = 0.0f;
	}
	Sparks.Reset();
	ShakeAmount = 0.0f;
	FovKick = 0.0f;
}

void AAmplitudeStage::HandleSimEvent(const Amp::FEvent& Event)
{
	const FAmplitudeSession* Session = GetSession();
	const bool bFever = Session != nullptr && Session->GetSimulation().IsFeverActive();
	const int32 Lane = FMath::Clamp(Event.Lane, 0, Amp::NumLanes - 1);
	const int32 Column = Event.Column >= 0 ? FMath::Clamp(Event.Column, 0, Amp::NumColumns - 1) : 1;
	const FVector Hit = GetHitPoint(Lane, Column);

	switch (Event.Type)
	{
	case Amp::EEventType::NoteHit:
	{
		TargetFlash[Lane][Column] = 1.0f;
		LaneFlash[Lane] = FMath::Max(LaneFlash[Lane], Event.bAuto ? 0.3f : 0.8f);
		if (Event.bAuto)
		{
			SpawnSparks(Hit, Lane, 4, 350.0f, 0.06f, 0.8f);
			break;
		}
		const bool bPerfect = Event.Judgement == Amp::EJudgement::Perfect;
		BeamFlash[Column] = 1.0f;
		SpawnSparks(Hit, Lane, bPerfect ? 16 : 10, bPerfect ? 900.0f : 650.0f, 0.1f, 0.75f);
		if (bPerfect)
		{
			SpawnSparks(Hit, GoldSlot, 6, 700.0f, 0.07f, 0.9f);
			FovKick = FMath::Max(FovKick, 0.5f);
		}
		AddShake(bFever ? 7.0f : (bPerfect ? 3.0f : 1.5f));
		break;
	}

	case Amp::EEventType::NoteMissed:
		MissFlash[Lane] = 1.0f;
		SpawnSparks(Hit, RedSlot, 6, 300.0f, 0.08f, 0.2f);
		AddShake(2.0f);
		break;

	case Amp::EEventType::GhostPress:
		BeamFlash[Column] = 0.7f;
		TargetFlash[Lane][Column] = FMath::Max(TargetFlash[Lane][Column], 0.4f);
		break;

	case Amp::EEventType::LaneCaptured:
		CaptureFlash[Lane] = 1.0f;
		for (double Distance = 0.0; Distance <= TrackLength; Distance += 320.0)
		{
			SpawnSparks(TrackPoint(Distance, LaneCenter(Lane), 30.0), Lane, 2, 260.0f, 0.12f, 1.0f);
		}
		SpawnSparks(Hit, WhiteSlot, 20, 1100.0f, 0.1f, 0.9f);
		AddShake(6.0f);
		FovKick = 1.0f;
		break;

	case Amp::EEventType::PowerupCollected:
		SpawnSparks(ShipLocation, PowerupSlot(Event.Powerup), 26, 1000.0f, 0.12f, 0.7f);
		AddShake(5.0f);
		FovKick = FMath::Max(FovKick, 0.7f);
		break;

	case Amp::EEventType::LaneCleared:
		for (double Distance = 0.0; Distance <= TrackLength * 0.5; Distance += 220.0)
		{
			SpawnSparks(TrackPoint(Distance, LaneCenter(Lane), 30.0), PowerupSlot(Amp::EPowerupType::LaneCleaner), 3, 400.0f, 0.1f, 1.0f);
		}
		break;

	case Amp::EEventType::ShieldAbsorbedMiss:
		SpawnSparks(ShipLocation, PowerupSlot(Amp::EPowerupType::Shield), 18, 800.0f, 0.1f, 0.6f);
		break;

	case Amp::EEventType::SongComplete:
		for (int32 Burst = 0; Burst < Amp::NumLanes; ++Burst)
		{
			SpawnSparks(GetHitPoint(Burst, 1), Burst, 24, 1200.0f, 0.12f, 0.9f);
		}
		FovKick = 1.0f;
		break;

	case Amp::EEventType::GameOver:
		SpawnSparks(ShipLocation, RedSlot, 30, 900.0f, 0.12f, 0.5f);
		AddShake(18.0f);
		break;

	default:
		break;
	}
}
