#pragma once

#include "CoreMinimal.h"
#include "Core/AmpSimulation.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"

class AAmplitudeDirector;
class FAmplitudeSession;

/**
 * Draws the playfield and in-game HUD (spec 11.1, 12): six lanes with falling notes, hit zones,
 * the Beat Blaster, powerups, particles, judgement pop-ups, screen shake and the top/bottom bars.
 * With no active session it draws an animated attract-mode backdrop for the menus.
 */
class SAmplitudeGameView : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAmplitudeGameView) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AAmplitudeDirector>, Director)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Spawns the visual feedback for a simulation event. */
	void HandleSimEvent(const Amp::FEvent& Event);
	void ResetEffects();

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	/** Particle/pop-up positions are stored in normalised playfield space (x: 0-1 across, y: 0-1 down). */
	struct FParticle
	{
		FVector2D Position;
		FVector2D Velocity;
		FLinearColor Color;
		float Age = 0.0f;
		float Life = 0.5f;
		float Size = 6.0f;
	};

	struct FPopup
	{
		FString Text;
		FLinearColor Color;
		FVector2D Position;
		float Age = 0.0f;
		float Life = 0.6f;
		float Size = 22.0f;
		float Rise = 0.06f;
	};

	struct FBanner
	{
		FString Text;
		FLinearColor Color;
		float Age = 0.0f;
		float Life = 1.2f;
	};

	struct FRing
	{
		FVector2D Position;
		FLinearColor Color;
		float Age = 0.0f;
		float Life = 0.6f;
		float MaxRadius = 0.2f;
	};

	/** Screen-space layout of the playfield for the current paint. */
	struct FLayout
	{
		FVector2D Size = FVector2D::ZeroVector;
		float TopBar = 0.0f;
		float BottomBar = 0.0f;
		float FieldLeft = 0.0f;
		float FieldTop = 0.0f;
		float FieldWidth = 0.0f;
		float FieldHeight = 0.0f;
		float LaneWidth = 0.0f;
		float HitLineY = 0.0f;
		float ShipY = 0.0f;
		FVector2D Shake = FVector2D::ZeroVector;
		float Zoom = 1.0f;
		bool bDesaturate = false;

		FVector2D ToScreen(const FVector2D& Normalized) const;
		/** Normalised x of a lane's centre. */
		static float LaneCenterX(int32 Lane);
	};

	FLayout ComputeLayout(const FGeometry& Geometry, const Amp::FSimulation* Simulation) const;
	float NoteY(const FLayout& Layout, const Amp::FSimulation& Simulation, double NoteTimeMs, double SongTimeMs) const;

	int32 PaintBackground(const FLayout& Layout, const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FAmplitudeSession* Session) const;
	int32 PaintLanes(const FLayout& Layout, const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FAmplitudeSession& Session) const;
	int32 PaintNotes(const FLayout& Layout, const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FAmplitudeSession& Session) const;
	int32 PaintPowerups(const FLayout& Layout, const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FAmplitudeSession& Session) const;
	int32 PaintShip(const FLayout& Layout, const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FAmplitudeSession& Session) const;
	int32 PaintEffects(const FLayout& Layout, const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer) const;
	int32 PaintHud(const FLayout& Layout, const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FAmplitudeSession& Session) const;
	int32 PaintCenterMessages(const FLayout& Layout, const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FAmplitudeSession& Session) const;
	int32 PaintAttract(const FLayout& Layout, const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer) const;

	FLinearColor Tint(const FLayout& Layout, const FLinearColor& Color) const;
	void SpawnBurst(int32 Lane, float Y, const FLinearColor& Color, int32 Count, float Speed, bool bDownward);
	void AddPopup(const FString& Text, const FLinearColor& Color, int32 Lane, float Y, float Size, float Life);
	void AddBanner(const FString& Text, const FLinearColor& Color, float Life = 1.2f);
	void AddShake(float Pixels);
	const FAmplitudeSession* GetSession() const;
	float GetHitLineNormalized() const;
	float GetShipNormalized() const;

	TWeakObjectPtr<AAmplitudeDirector> Director;

	TArray<FParticle> Particles;
	TArray<FPopup> Popups;
	TArray<FBanner> Banners;
	TArray<FRing> Rings;
	float LaneFlash[6] = {};
	float LaneMissFlash[6] = {};
	float ShakeAmplitude = 0.0f;
	FVector2D ShakeOffset = FVector2D::ZeroVector;
	float ZoomPulse = 0.0f;
	float FeverFlash = 0.0f;
	float ShipTrail = 0.0f;
	int32 TrailFromLane = -1;
	double Time = 0.0;
	float SmoothedFps = 60.0f;
};
