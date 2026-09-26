#pragma once

#include "CoreMinimal.h"
#include "Core/AmpSimulation.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"

class AAmplitudeDirector;
class FAmplitudeSession;

/**
 * The in-game HUD drawn over the 3D stage (spec 11.1, 11.2): score and multiplier, song progress,
 * energy, a strip with every lane's state, active powerups, button prompts, judgement pop-ups
 * anchored to the 3D hit line, banners and the countdowns. Draws nothing without a session.
 */
class SAmplitudeGameView : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAmplitudeGameView) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AAmplitudeDirector>, Director)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Pop-ups and banners for a simulation event. */
	void HandleSimEvent(const Amp::FEvent& Event);
	void ResetEffects();

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	/** Text that rises from a point in the 3D world. */
	struct FPopup
	{
		FString Text;
		FLinearColor Color;
		FVector WorldLocation = FVector::ZeroVector;
		/** Screen offset (in units of the HUD scale) from the projected point. */
		FVector2D Offset = FVector2D::ZeroVector;
		float Age = 0.0f;
		float Life = 0.6f;
		float Size = 22.0f;
	};

	struct FBanner
	{
		FString Text;
		FLinearColor Color;
		float Age = 0.0f;
		float Life = 1.2f;
	};

	int32 PaintTopBar(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FAmplitudeSession& Session, float Scale) const;
	int32 PaintLaneStrip(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FAmplitudeSession& Session, float Scale) const;
	int32 PaintPowerups(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FAmplitudeSession& Session, float Scale) const;
	int32 PaintPrompts(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FAmplitudeSession& Session, float Scale) const;
	int32 PaintPopups(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, float Scale) const;
	int32 PaintCenterMessages(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FAmplitudeSession& Session, float Scale) const;

	void AddPopup(const FString& Text, const FLinearColor& Color, const FVector& WorldLocation, float Size, float Life, const FVector2D& Offset = FVector2D::ZeroVector);
	void AddBanner(const FString& Text, const FLinearColor& Color, float Life = 1.2f);
	bool ProjectToLocal(const FVector& WorldLocation, const FVector2D& LocalSize, FVector2D& OutLocal) const;
	FVector GetHitPoint(int32 Lane, int32 Column) const;
	FVector GetShipLocation() const;
	const FAmplitudeSession* GetSession() const;

	TWeakObjectPtr<AAmplitudeDirector> Director;

	TArray<FPopup> Popups;
	TArray<FBanner> Banners;
	float FeverFlash = 0.0f;
	float ScorePulse = 0.0f;
	double Time = 0.0;
	float SmoothedFps = 60.0f;
};
