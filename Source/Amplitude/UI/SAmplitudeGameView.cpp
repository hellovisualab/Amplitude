#include "UI/SAmplitudeGameView.h"

#include "Core/AmpRules.h"
#include "Data/AmplitudeSongLibrary.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Game/AmplitudeDirector.h"
#include "Game/AmplitudeSession.h"
#include "Game/AmplitudeUserSettings.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "UI/AmplitudeStyle.h"

#include <algorithm>

namespace
{
	constexpr int32 MaxParticles = 700;
	constexpr float DefaultHitLine = 0.82f;
	constexpr float DefaultShipLine = 0.9f;
	/** Missed notes keep falling (and fading) for this long after they are judged. */
	constexpr double MissFadeMs = 450.0;

	FLinearColor WithAlpha(const FLinearColor& Color, float Alpha)
	{
		FLinearColor Result = Color;
		Result.A = Alpha;
		return Result;
	}

	void DrawBox(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2D& Position, const FVector2D& Size, const FLinearColor& Color, const FSlateBrush* Brush = nullptr)
	{
		if (Size.X <= 0.0 || Size.Y <= 0.0 || Color.A <= 0.001f)
		{
			return;
		}
		FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Position)),
			Brush != nullptr ? Brush : AmplitudeStyle::WhiteBrush(), ESlateDrawEffect::None, Color);
	}

	void DrawCenteredBox(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2D& Center, const FVector2D& Size, const FLinearColor& Color, const FSlateBrush* Brush = nullptr)
	{
		DrawBox(Out, Layer, Geometry, Center - Size * 0.5, Size, Color, Brush);
	}

	void DrawRotatedBox(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2D& Center, const FVector2D& Size, float AngleRadians, const FLinearColor& Color, const FSlateBrush* Brush = nullptr)
	{
		if (Color.A <= 0.001f)
		{
			return;
		}
		const FGeometry Child = Geometry.MakeChild(Size, FSlateLayoutTransform(Center - Size * 0.5), FSlateRenderTransform(FQuat2D(AngleRadians)), FVector2D(0.5, 0.5));
		FSlateDrawElement::MakeBox(Out, Layer, Child.ToPaintGeometry(), Brush != nullptr ? Brush : AmplitudeStyle::WhiteBrush(), ESlateDrawEffect::None, Color);
	}

	void DrawLines(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const TArray<FVector2D>& Points, const FLinearColor& Color, float Thickness)
	{
		if (Points.Num() < 2 || Color.A <= 0.001f)
		{
			return;
		}
		TArray<FVector2f> LocalPoints;
		LocalPoints.Reserve(Points.Num());
		for (const FVector2D& Point : Points)
		{
			LocalPoints.Add(FVector2f(Point));
		}
		FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), MoveTemp(LocalPoints), ESlateDrawEffect::None, Color, true, Thickness);
	}

	void DrawRectOutline(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2D& Position, const FVector2D& Size, const FLinearColor& Color, float Thickness)
	{
		const TArray<FVector2D> Points = {
			Position,
			Position + FVector2D(Size.X, 0.0),
			Position + Size,
			Position + FVector2D(0.0, Size.Y),
			Position};
		DrawLines(Out, Layer, Geometry, Points, Color, Thickness);
	}

	void DrawCircle(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2D& Center, float Radius, const FLinearColor& Color, float Thickness)
	{
		constexpr int32 Segments = 40;
		TArray<FVector2D> Points;
		Points.Reserve(Segments + 1);
		for (int32 Index = 0; Index <= Segments; ++Index)
		{
			const double Angle = 2.0 * UE_DOUBLE_PI * Index / Segments;
			Points.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}
		DrawLines(Out, Layer, Geometry, Points, Color, Thickness);
	}

	/** Solid triangle drawn as horizontal spans (Slate has no filled-polygon primitive without a texture resource). */
	void DrawFilledTriangle(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color)
	{
		const FVector2D Vertices[3] = {A, B, C};
		const double MinY = FMath::Min3(A.Y, B.Y, C.Y);
		const double MaxY = FMath::Max3(A.Y, B.Y, C.Y);
		constexpr double Step = 1.5;
		for (double Y = MinY; Y <= MaxY; Y += Step)
		{
			double Left = TNumericLimits<double>::Max();
			double Right = TNumericLimits<double>::Lowest();
			for (int32 Edge = 0; Edge < 3; ++Edge)
			{
				const FVector2D& P = Vertices[Edge];
				const FVector2D& Q = Vertices[(Edge + 1) % 3];
				if (FMath::IsNearlyEqual(P.Y, Q.Y) || Y < FMath::Min(P.Y, Q.Y) || Y > FMath::Max(P.Y, Q.Y))
				{
					continue;
				}
				const double X = P.X + (Y - P.Y) * (Q.X - P.X) / (Q.Y - P.Y);
				Left = FMath::Min(Left, X);
				Right = FMath::Max(Right, X);
			}
			if (Right >= Left)
			{
				DrawLines(Out, Layer, Geometry, {FVector2D(Left, Y), FVector2D(Right, Y)}, Color, 2.0f);
			}
		}
	}

	FVector2D MeasureText(const FString& Text, const FSlateFontInfo& Font)
	{
		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		return FVector2D(Measure->Measure(Text, Font));
	}

	/** Draws text anchored at Anchor; Align (0-1 per axis) picks the anchor point inside the text box. */
	void PaintText(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FString& Text, const FSlateFontInfo& Font, const FVector2D& Anchor, const FLinearColor& Color, const FVector2D& Align = FVector2D::ZeroVector, bool bShadow = true)
	{
		if (Text.IsEmpty() || Color.A <= 0.001f)
		{
			return;
		}
		const FVector2D Size = MeasureText(Text, Font);
		const FVector2D Position = Anchor - Size * Align;
		if (bShadow)
		{
			FSlateDrawElement::MakeText(Out, Layer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Position + FVector2D(2.0, 2.0))), Text, Font, ESlateDrawEffect::None, FLinearColor(0.0f, 0.0f, 0.0f, Color.A * 0.6f));
		}
		FSlateDrawElement::MakeText(Out, Layer + 1, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Position)), Text, Font, ESlateDrawEffect::None, Color);
	}

	FString GetPowerupStatusName(Amp::EPowerupType Type)
	{
		switch (Type)
		{
		case Amp::EPowerupType::Score2x:
			return TEXT("SCORE 2X");
		case Amp::EPowerupType::LaneCleaner:
			return TEXT("LANE CLEANER");
		case Amp::EPowerupType::SlowMotion:
			return TEXT("SLOW MOTION");
		case Amp::EPowerupType::Shield:
			return TEXT("SHIELD");
		case Amp::EPowerupType::Fever:
			return TEXT("FEVER MODE");
		case Amp::EPowerupType::AutoCapture:
			return TEXT("AUTO-CAPTURE");
		}
		return FString();
	}

	FString Seconds(double Milliseconds)
	{
		return FString::Printf(TEXT("%ds"), FMath::CeilToInt32(FMath::Max(0.0, Milliseconds) / 1000.0));
	}
}

// ---------------------------------------------------------------------------------------------

FVector2D SAmplitudeGameView::FLayout::ToScreen(const FVector2D& Normalized) const
{
	const FVector2D Center(FieldLeft + FieldWidth * 0.5f, FieldTop + FieldHeight * 0.5f);
	const FVector2D Point(FieldLeft + Normalized.X * FieldWidth, FieldTop + Normalized.Y * FieldHeight);
	return Center + (Point - Center) * Zoom + Shake;
}

float SAmplitudeGameView::FLayout::LaneCenterX(int32 Lane)
{
	return (static_cast<float>(Lane) + 0.5f) / static_cast<float>(Amp::NumLanes);
}

void SAmplitudeGameView::Construct(const FArguments& InArgs)
{
	Director = InArgs._Director;
	SetCanTick(true);
}

FVector2D SAmplitudeGameView::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return FVector2D(640.0, 360.0);
}

const FAmplitudeSession* SAmplitudeGameView::GetSession() const
{
	const AAmplitudeDirector* Owner = Director.Get();
	return Owner != nullptr ? Owner->GetSession() : nullptr;
}

float SAmplitudeGameView::GetHitLineNormalized() const
{
	const FAmplitudeSession* Session = GetSession();
	return Session != nullptr ? static_cast<float>(Session->GetSimulation().GetRules().HitLineY) : DefaultHitLine;
}

float SAmplitudeGameView::GetShipNormalized() const
{
	const FAmplitudeSession* Session = GetSession();
	return Session != nullptr ? static_cast<float>(Session->GetSimulation().GetRules().ShipY) : DefaultShipLine;
}

void SAmplitudeGameView::ResetEffects()
{
	Particles.Reset();
	Popups.Reset();
	Banners.Reset();
	Rings.Reset();
	for (int32 Lane = 0; Lane < Amp::NumLanes; ++Lane)
	{
		LaneFlash[Lane] = 0.0f;
		LaneMissFlash[Lane] = 0.0f;
	}
	ShakeAmplitude = 0.0f;
	ShakeOffset = FVector2D::ZeroVector;
	ZoomPulse = 0.0f;
	FeverFlash = 0.0f;
	ShipTrail = 0.0f;
	TrailFromLane = -1;
}

// ---------------------------------------------------------------------------------------------
// Effects

void SAmplitudeGameView::SpawnBurst(int32 Lane, float Y, const FLinearColor& Color, int32 Count, float Speed, bool bDownward)
{
	const float X = (static_cast<float>(Lane) + 0.5f) / static_cast<float>(Amp::NumLanes);
	for (int32 Index = 0; Index < Count && Particles.Num() < MaxParticles; ++Index)
	{
		FParticle Particle;
		Particle.Position = FVector2D(X + FMath::FRandRange(-0.02f, 0.02f), Y);
		const float Angle = bDownward ? FMath::FRandRange(0.35f, 2.8f) : FMath::FRandRange(-3.0f, -0.14f);
		const float Magnitude = Speed * FMath::FRandRange(0.4f, 1.0f);
		// Horizontal speed is scaled down because the field is much wider than it is tall per lane.
		Particle.Velocity = FVector2D(FMath::Cos(Angle) * Magnitude * 0.35f, FMath::Sin(Angle) * Magnitude);
		Particle.Color = Color;
		Particle.Life = FMath::FRandRange(0.35f, 0.7f);
		Particle.Size = FMath::FRandRange(3.0f, 7.0f);
		Particles.Add(Particle);
	}
}

void SAmplitudeGameView::AddPopup(const FString& Text, const FLinearColor& Color, int32 Lane, float Y, float Size, float Life)
{
	FPopup Popup;
	Popup.Text = Text;
	Popup.Color = Color;
	Popup.Position = FVector2D((static_cast<float>(Lane) + 0.5f) / static_cast<float>(Amp::NumLanes), Y);
	Popup.Size = Size;
	Popup.Life = Life;
	Popups.Add(Popup);
}

void SAmplitudeGameView::AddBanner(const FString& Text, const FLinearColor& Color, float Life)
{
	// Only one banner at a time: the newest message wins.
	Banners.Reset();
	FBanner Banner;
	Banner.Text = Text;
	Banner.Color = Color;
	Banner.Life = Life;
	Banners.Add(Banner);
}

void SAmplitudeGameView::AddShake(float Pixels)
{
	ShakeAmplitude = FMath::Max(ShakeAmplitude, Pixels);
}

void SAmplitudeGameView::HandleSimEvent(const Amp::FEvent& Event)
{
	const FAmplitudeSession* Session = GetSession();
	const bool bFever = Session != nullptr && Session->GetSimulation().IsFeverActive();
	const float HitY = GetHitLineNormalized();
	const float ShipY = GetShipNormalized();
	const int32 Lane = FMath::Clamp(Event.Lane, 0, Amp::NumLanes - 1);
	const FLinearColor LaneColor = AmplitudeStyle::GetLaneColor(Lane);

	switch (Event.Type)
	{
	case Amp::EEventType::NoteHit:
	{
		const bool bPerfect = Event.Judgement == Amp::EJudgement::Perfect;
		LaneFlash[Lane] = 1.0f;
		if (Event.bAuto)
		{
			SpawnBurst(Lane, HitY, LaneColor, 5, 0.35f, false);
			break;
		}
		const FLinearColor Color = bPerfect ? AmplitudeStyle::Perfect : AmplitudeStyle::Good;
		SpawnBurst(Lane, HitY, Color, bPerfect ? 16 : 9, bPerfect ? 0.7f : 0.5f, false);
		AddPopup(bPerfect ? TEXT("PERFECT") : TEXT("GOOD"), Color, Lane, HitY - 0.05f, 22.0f, 0.55f);
		AddPopup(FString::Printf(TEXT("+%lld"), static_cast<long long>(Event.Points)), WithAlpha(Color, 0.9f), Lane, HitY - 0.1f, 15.0f, 0.5f);
		// Spec 12.6: Perfect = 2-3 px shake and a slight zoom, Good = smaller shake, Fever = 5-8 px.
		AddShake(bFever ? 5.0f : (bPerfect ? 2.5f : 1.5f));
		if (bPerfect)
		{
			ZoomPulse = 1.0f;
		}
		break;
	}

	case Amp::EEventType::NoteMissed:
		LaneMissFlash[Lane] = 1.0f;
		SpawnBurst(Lane, HitY, AmplitudeStyle::Miss, 8, 0.45f, true);
		AddPopup(TEXT("MISS"), AmplitudeStyle::Miss, Lane, HitY - 0.05f, 22.0f, 0.6f);
		break;

	case Amp::EEventType::GhostPress:
		LaneFlash[Lane] = FMath::Max(LaneFlash[Lane], 0.35f);
		break;

	case Amp::EEventType::ShipMoved:
		TrailFromLane = Event.Count;
		ShipTrail = 1.0f;
		break;

	case Amp::EEventType::LaneCaptured:
	{
		FRing Ring;
		Ring.Position = FVector2D(FLayout::LaneCenterX(Lane), HitY);
		Ring.Color = LaneColor;
		Ring.Life = 0.7f;
		Ring.MaxRadius = 0.25f;
		Rings.Add(Ring);
		LaneFlash[Lane] = 1.0f;
		AddPopup(TEXT("CAPTURED!"), LaneColor, Lane, 0.3f, 24.0f, 1.0f);
		AddPopup(FString::Printf(TEXT("-%d ENERGY"), Event.Count), WithAlpha(AmplitudeStyle::TextDim, 0.9f), Lane, 0.35f, 13.0f, 1.0f);
		break;
	}

	case Amp::EEventType::CaptureExpired:
		AddPopup(TEXT("RELEASED"), WithAlpha(LaneColor, 0.8f), Lane, 0.3f, 16.0f, 0.9f);
		break;

	case Amp::EEventType::LaneMuted:
		AddPopup(TEXT("MUTED"), AmplitudeStyle::Miss, Lane, 0.45f, 24.0f, 1.0f);
		break;

	case Amp::EEventType::LaneUnmuted:
		AddPopup(TEXT("BACK IN THE MIX"), LaneColor, Lane, 0.45f, 16.0f, 0.9f);
		break;

	case Amp::EEventType::PowerupCollected:
	{
		const FLinearColor Color = AmplitudeStyle::GetPowerupColor(Event.Powerup);
		FString Message = GetPowerupStatusName(Event.Powerup) + TEXT("!");
		if (Event.Powerup == Amp::EPowerupType::LaneCleaner)
		{
			Message = TEXT("LANE CLEANER - PRESS A LANE TO CLEAR IT");
		}
		AddBanner(Message, Color, Event.Powerup == Amp::EPowerupType::LaneCleaner ? 3.0f : 1.4f);
		FRing Ring;
		Ring.Position = FVector2D(FLayout::LaneCenterX(Lane), ShipY);
		Ring.Color = Color;
		Rings.Add(Ring);
		SpawnBurst(Lane, ShipY, Color, 18, 0.8f, false);
		AddPopup(FString::Printf(TEXT("+%lld"), static_cast<long long>(Event.Points)), Color, Lane, ShipY - 0.06f, 18.0f, 0.8f);
		AddShake(Event.Powerup == Amp::EPowerupType::Fever ? 7.0f : 4.5f);
		if (Event.Powerup == Amp::EPowerupType::Fever)
		{
			FeverFlash = 1.0f;
		}
		break;
	}

	case Amp::EEventType::LaneCleared:
	{
		const FLinearColor Color = AmplitudeStyle::GetPowerupColor(Amp::EPowerupType::LaneCleaner);
		for (int32 Step = 0; Step < 6; ++Step)
		{
			SpawnBurst(Lane, 0.1f + Step * 0.13f, Color, 5, 0.4f, false);
		}
		AddPopup(FString::Printf(TEXT("CLEARED %d"), Event.Count), Color, Lane, 0.4f, 22.0f, 1.0f);
		Banners.Reset();
		break;
	}

	case Amp::EEventType::ShieldAbsorbedMiss:
	{
		FRing Ring;
		Ring.Position = FVector2D(FLayout::LaneCenterX(Lane), HitY);
		Ring.Color = AmplitudeStyle::Shield;
		Rings.Add(Ring);
		AddPopup(TEXT("SHIELDED"), AmplitudeStyle::Shield, Lane, HitY - 0.14f, 18.0f, 0.8f);
		break;
	}

	case Amp::EEventType::FeverBroken:
		AddBanner(TEXT("FEVER BROKEN"), WithAlpha(AmplitudeStyle::Fever, 0.8f), 1.0f);
		break;

	case Amp::EEventType::AutoCaptureFailed:
		AddBanner(TEXT("AUTO-CAPTURE FIZZLED - NOT ENOUGH ENERGY"), AmplitudeStyle::TextDim, 1.4f);
		break;

	case Amp::EEventType::EnergyLow:
		AddBanner(TEXT("ENERGY LOW!"), AmplitudeStyle::EnergyLow, 1.2f);
		break;

	case Amp::EEventType::GameOver:
		AddBanner(TEXT("GAME OVER"), AmplitudeStyle::Miss, 10.0f);
		AddShake(8.0f);
		break;

	case Amp::EEventType::SongComplete:
		AddBanner(TEXT("SONG COMPLETE!"), AmplitudeStyle::Perfect, 10.0f);
		for (int32 Burst = 0; Burst < Amp::NumLanes; ++Burst)
		{
			SpawnBurst(Burst, HitY, AmplitudeStyle::GetLaneColor(Burst), 20, 0.9f, false);
		}
		break;

	default:
		break;
	}
}

void SAmplitudeGameView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SLeafWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	const float Dt = FMath::Clamp(InDeltaTime, 0.0f, 0.1f);
	Time += Dt;
	if (InDeltaTime > 0.0f)
	{
		SmoothedFps = FMath::Lerp(SmoothedFps, 1.0f / InDeltaTime, 0.05f);
	}

	for (int32 Index = Particles.Num() - 1; Index >= 0; --Index)
	{
		FParticle& Particle = Particles[Index];
		Particle.Age += Dt;
		if (Particle.Age >= Particle.Life)
		{
			Particles.RemoveAtSwap(Index);
			continue;
		}
		Particle.Position += Particle.Velocity * Dt;
		Particle.Velocity.Y += 0.9 * Dt;
		Particle.Velocity *= FMath::Pow(0.08f, Dt);
	}
	for (int32 Index = Popups.Num() - 1; Index >= 0; --Index)
	{
		Popups[Index].Age += Dt;
		if (Popups[Index].Age >= Popups[Index].Life)
		{
			Popups.RemoveAt(Index);
		}
	}
	for (int32 Index = Rings.Num() - 1; Index >= 0; --Index)
	{
		Rings[Index].Age += Dt;
		if (Rings[Index].Age >= Rings[Index].Life)
		{
			Rings.RemoveAtSwap(Index);
		}
	}
	for (int32 Index = Banners.Num() - 1; Index >= 0; --Index)
	{
		Banners[Index].Age += Dt;
		if (Banners[Index].Age >= Banners[Index].Life)
		{
			Banners.RemoveAt(Index);
		}
	}

	for (int32 Lane = 0; Lane < Amp::NumLanes; ++Lane)
	{
		LaneFlash[Lane] = FMath::Max(0.0f, LaneFlash[Lane] - Dt * 5.0f);
		LaneMissFlash[Lane] = FMath::Max(0.0f, LaneMissFlash[Lane] - Dt * 3.0f);
	}

	// Shake decays back to centre in roughly 200 ms (spec 12.6.3).
	ShakeAmplitude *= FMath::Exp(-Dt * 15.0f);
	if (ShakeAmplitude < 0.1f)
	{
		ShakeAmplitude = 0.0f;
	}
	ShakeOffset = FVector2D(FMath::FRandRange(-1.0f, 1.0f), FMath::FRandRange(-1.0f, 1.0f)) * ShakeAmplitude;
	ZoomPulse *= FMath::Exp(-Dt * 14.0f);
	FeverFlash = FMath::Max(0.0f, FeverFlash - Dt * 2.0f);
	ShipTrail = FMath::Max(0.0f, ShipTrail - Dt / 0.12f);
}

// ---------------------------------------------------------------------------------------------
// Painting

SAmplitudeGameView::FLayout SAmplitudeGameView::ComputeLayout(const FGeometry& Geometry, const Amp::FSimulation* Simulation) const
{
	FLayout Layout;
	Layout.Size = FVector2D(Geometry.GetLocalSize());
	const float Width = static_cast<float>(Layout.Size.X);
	const float Height = static_cast<float>(Layout.Size.Y);
	Layout.TopBar = FMath::Max(44.0f, Height * 0.075f);
	Layout.BottomBar = FMath::Max(84.0f, Height * 0.13f);
	Layout.FieldLeft = Width * 0.03f;
	Layout.FieldWidth = Width - Layout.FieldLeft * 2.0f;
	Layout.FieldTop = Layout.TopBar + 6.0f;
	Layout.FieldHeight = FMath::Max(10.0f, Height - Layout.TopBar - Layout.BottomBar - 12.0f);
	Layout.LaneWidth = Layout.FieldWidth / static_cast<float>(Amp::NumLanes);
	const float HitNorm = Simulation != nullptr ? static_cast<float>(Simulation->GetRules().HitLineY) : DefaultHitLine;
	const float ShipNorm = Simulation != nullptr ? static_cast<float>(Simulation->GetRules().ShipY) : DefaultShipLine;
	Layout.HitLineY = Layout.FieldTop + Layout.FieldHeight * HitNorm;
	Layout.ShipY = Layout.FieldTop + Layout.FieldHeight * ShipNorm;
	Layout.Shake = ShakeOffset;
	Layout.Zoom = 1.0f + ZoomPulse * 0.012f;
	Layout.bDesaturate = Simulation != nullptr && Simulation->IsSlowMotionActive();
	return Layout;
}

FLinearColor SAmplitudeGameView::Tint(const FLayout& Layout, const FLinearColor& Color) const
{
	if (!Layout.bDesaturate)
	{
		return Color;
	}
	// Slow Motion: reduce saturation by 30% (spec 12.6.2).
	const float Luminance = Color.R * 0.3f + Color.G * 0.59f + Color.B * 0.11f;
	return FLinearColor(FMath::Lerp(Color.R, Luminance, 0.3f), FMath::Lerp(Color.G, Luminance, 0.3f), FMath::Lerp(Color.B, Luminance, 0.3f), Color.A);
}

float SAmplitudeGameView::NoteY(const FLayout& Layout, const Amp::FSimulation& Simulation, double NoteTimeMs, double SongTimeMs) const
{
	const double Approach = FMath::Max(1.0, Simulation.GetParams().ApproachTimeMs);
	const double HitNorm = Simulation.GetRules().HitLineY;
	return static_cast<float>(HitNorm * (1.0 - (NoteTimeMs - SongTimeMs) / Approach));
}

int32 SAmplitudeGameView::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FAmplitudeSession* Session = GetSession();
	const FLayout Layout = ComputeLayout(AllottedGeometry, Session != nullptr ? &Session->GetSimulation() : nullptr);

	int32 Layer = PaintBackground(Layout, AllottedGeometry, OutDrawElements, LayerId, Session);
	if (Session == nullptr)
	{
		return PaintAttract(Layout, AllottedGeometry, OutDrawElements, Layer);
	}

	Layer = PaintLanes(Layout, AllottedGeometry, OutDrawElements, Layer, *Session);
	Layer = PaintNotes(Layout, AllottedGeometry, OutDrawElements, Layer, *Session);
	Layer = PaintPowerups(Layout, AllottedGeometry, OutDrawElements, Layer, *Session);
	Layer = PaintShip(Layout, AllottedGeometry, OutDrawElements, Layer, *Session);
	Layer = PaintEffects(Layout, AllottedGeometry, OutDrawElements, Layer);
	Layer = PaintHud(Layout, AllottedGeometry, OutDrawElements, Layer, *Session);
	return PaintCenterMessages(Layout, AllottedGeometry, OutDrawElements, Layer, *Session);
}

int32 SAmplitudeGameView::PaintBackground(const FLayout& Layout, const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FAmplitudeSession* Session) const
{
	DrawBox(Out, Layer, Geometry, FVector2D::ZeroVector, Layout.Size, AmplitudeStyle::Background);
	// A soft vertical gradient (black at the top to dark grey at the bottom, spec 12.2.2).
	constexpr int32 Bands = 8;
	for (int32 Band = 0; Band < Bands; ++Band)
	{
		const float Top = static_cast<float>(Layout.Size.Y) * Band / Bands;
		DrawBox(Out, Layer, Geometry, FVector2D(0.0, Top), FVector2D(Layout.Size.X, Layout.Size.Y / Bands + 1.0), FLinearColor(0.05f, 0.05f, 0.08f, 0.04f * Band));
	}

	if (Session != nullptr && Session->GetSimulation().IsFeverActive())
	{
		// Fever Mode: red/orange tint over the screen (spec 12.6.2), stronger for a moment on pickup.
		const float Pulse = 0.5f + 0.5f * FMath::Sin(static_cast<float>(Time) * 8.0f);
		DrawBox(Out, Layer + 1, Geometry, FVector2D::ZeroVector, Layout.Size, WithAlpha(AmplitudeStyle::Fever, 0.08f + 0.04f * Pulse + 0.25f * FeverFlash));
	}
	return Layer + 2;
}

int32 SAmplitudeGameView::PaintAttract(const FLayout& Layout, const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer) const
{
	// Menu backdrop: faint lanes with notes drifting down to the beat of nothing in particular.
	for (int32 Lane = 0; Lane < Amp::NumLanes; ++Lane)
	{
		const FLinearColor Color = AmplitudeStyle::GetLaneColor(Lane);
		const float Left = Layout.FieldLeft + Layout.LaneWidth * Lane;
		DrawBox(Out, Layer, Geometry, FVector2D(Left, 0.0), FVector2D(Layout.LaneWidth, Layout.Size.Y), WithAlpha(Color, 0.025f));
		DrawBox(Out, Layer, Geometry, FVector2D(Left, 0.0), FVector2D(1.0, Layout.Size.Y), FLinearColor(1.0f, 1.0f, 1.0f, 0.04f));

		for (int32 Note = 0; Note < 5; ++Note)
		{
			const float Speed = 0.08f + 0.03f * ((Lane * 7 + Note * 3) % 5);
			const float Phase = FMath::Frac(static_cast<float>(Time) * Speed + (Lane * 0.37f + Note * 0.21f));
			const FVector2D Center(Left + Layout.LaneWidth * 0.5f, Phase * Layout.Size.Y);
			const FVector2D Size(FMath::Clamp(Layout.LaneWidth * 0.4f, 20.0f, 110.0f), FMath::Clamp(Layout.LaneWidth * 0.14f, 10.0f, 30.0f));
			DrawCenteredBox(Out, Layer + 1, Geometry, Center, Size + FVector2D(14.0, 14.0), WithAlpha(Color, 0.05f), AmplitudeStyle::RoundedBrush());
			DrawCenteredBox(Out, Layer + 2, Geometry, Center, Size, WithAlpha(Color, 0.22f), AmplitudeStyle::RoundedBrush());
		}
	}
	return Layer + 3;
}

int32 SAmplitudeGameView::PaintLanes(const FLayout& Layout, const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FAmplitudeSession& Session) const
{
	const Amp::FSimulation& Simulation = Session.GetSimulation();
	const UAmplitudeUserSettings* Settings = UAmplitudeUserSettings::Get();
	const AAmplitudeDirector* Owner = Director.Get();
	const bool bGamepad = Owner != nullptr && Owner->IsUsingGamepad();
	const float BeatPulse = FMath::Pow(1.0f - static_cast<float>(Session.GetBeatPhase()), 3.0f);
	const float CleanerPulse = Simulation.IsLaneCleanerArmed() ? 0.5f + 0.5f * FMath::Sin(static_cast<float>(Time) * 12.0f) : 0.0f;
	const FSlateFontInfo LabelFont = AmplitudeStyle::Font(FMath::Clamp(Layout.LaneWidth * 0.075f, 10.0f, 18.0f));
	const FSlateFontInfo KeyFont = AmplitudeStyle::Font(FMath::Clamp(Layout.LaneWidth * 0.07f, 10.0f, 16.0f));
	const float NoteHeight = FMath::Clamp(Layout.LaneWidth * 0.46f * 0.36f, 12.0f, 34.0f);

	for (int32 Lane = 0; Lane < Amp::NumLanes; ++Lane)
	{
		const Amp::FLaneState& State = Simulation.GetLane(Lane);
		const FLinearColor Color = Tint(Layout, AmplitudeStyle::GetLaneColor(Lane));
		const FVector2D TopLeft = Layout.ToScreen(FVector2D(static_cast<double>(Lane) / Amp::NumLanes, 0.0));
		const FVector2D BottomRight = Layout.ToScreen(FVector2D(static_cast<double>(Lane + 1) / Amp::NumLanes, 1.0));
		const FVector2D LaneSize = BottomRight - TopLeft;

		// Lane body with a gentle gradient in the lane's colour.
		DrawBox(Out, Layer, Geometry, TopLeft, LaneSize, WithAlpha(Color, 0.035f));
		DrawBox(Out, Layer, Geometry, TopLeft + FVector2D(0.0, LaneSize.Y * 0.5), FVector2D(LaneSize.X, LaneSize.Y * 0.5), WithAlpha(Color, 0.03f));

		if (State.bCaptured)
		{
			const float Pulse = 0.5f + 0.5f * FMath::Sin(static_cast<float>(Time) * 6.0f);
			DrawBox(Out, Layer + 1, Geometry, TopLeft, LaneSize, WithAlpha(Color, 0.10f + 0.08f * Pulse));
			DrawRectOutline(Out, Layer + 1, Geometry, TopLeft, LaneSize, WithAlpha(Color, 0.6f + 0.3f * Pulse), 2.0f);
		}
		if (State.bMuted)
		{
			DrawBox(Out, Layer + 1, Geometry, TopLeft, LaneSize, FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
		}
		if (CleanerPulse > 0.0f && !State.bCaptured)
		{
			DrawRectOutline(Out, Layer + 1, Geometry, TopLeft + FVector2D(3.0, 3.0), LaneSize - FVector2D(6.0, 6.0),
				WithAlpha(AmplitudeStyle::GetPowerupColor(Amp::EPowerupType::LaneCleaner), 0.4f + 0.5f * CleanerPulse), 3.0f);
		}
		if (LaneMissFlash[Lane] > 0.0f)
		{
			DrawBox(Out, Layer + 1, Geometry, TopLeft, LaneSize, WithAlpha(AmplitudeStyle::Miss, 0.12f * LaneMissFlash[Lane]));
		}

		// Separator.
		DrawBox(Out, Layer + 1, Geometry, TopLeft, FVector2D(1.5, LaneSize.Y), FLinearColor(1.0f, 1.0f, 1.0f, 0.08f));

		// Hit zone (spec 11.2.2): darker target area that pulses with the beat and flashes on hits.
		const FVector2D ZoneCenter = Layout.ToScreen(FVector2D(Layout.LaneCenterX(Lane), (Layout.HitLineY - Layout.FieldTop) / Layout.FieldHeight));
		const FVector2D ZoneSize(LaneSize.X * 0.84, NoteHeight * 1.9 * Layout.Zoom);
		DrawCenteredBox(Out, Layer + 1, Geometry, ZoneCenter, ZoneSize, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), AmplitudeStyle::RoundedBrush());
		DrawCenteredBox(Out, Layer + 2, Geometry, ZoneCenter, ZoneSize, WithAlpha(Color, 0.08f + 0.12f * BeatPulse + 0.45f * LaneFlash[Lane]), AmplitudeStyle::RoundedBrush());
		DrawRectOutline(Out, Layer + 2, Geometry, ZoneCenter - ZoneSize * 0.5, ZoneSize, WithAlpha(Color, 0.55f + 0.35f * LaneFlash[Lane]), 2.0f);

		// Instrument name at the top of the lane and the key to press under the hit zone.
		PaintText(Out, Layer + 3, Geometry, AmplitudeStyle::GetLaneLabel(Lane).ToString(), LabelFont,
			FVector2D(ZoneCenter.X, TopLeft.Y + 6.0), WithAlpha(Color, State.bMuted ? 0.35f : 0.75f), FVector2D(0.5, 0.0));
		if (Settings != nullptr)
		{
			const FKey Key = Settings->GetActiveProfile().GetDisplayKey(Lane, bGamepad);
			PaintText(Out, Layer + 3, Geometry, AmplitudeStyle::GetKeyLabel(Key).ToString(), KeyFont,
				FVector2D(ZoneCenter.X, ZoneCenter.Y + ZoneSize.Y * 0.5 + 4.0), WithAlpha(AmplitudeStyle::TextDim, 0.8f), FVector2D(0.5, 0.0));
		}

		if (State.bCaptured)
		{
			PaintText(Out, Layer + 3, Geometry, FString::Printf(TEXT("CAPTURED %s"), *Seconds(Simulation.GetCaptureRemainingMs(Lane))), LabelFont,
				FVector2D(ZoneCenter.X, TopLeft.Y + 28.0), Color, FVector2D(0.5, 0.0));
		}
		if (State.bMuted)
		{
			PaintText(Out, Layer + 3, Geometry, TEXT("MUTED"), AmplitudeStyle::Font(FMath::Clamp(Layout.LaneWidth * 0.11f, 12.0f, 26.0f)),
				FVector2D(ZoneCenter.X, TopLeft.Y + LaneSize.Y * 0.45), WithAlpha(AmplitudeStyle::Miss, 0.85f), FVector2D(0.5, 0.5));
		}
	}

	// Right edge separator.
	const FVector2D FieldRight = Layout.ToScreen(FVector2D(1.0, 0.0));
	DrawBox(Out, Layer + 1, Geometry, FieldRight, FVector2D(1.5, Layout.FieldHeight * Layout.Zoom), FLinearColor(1.0f, 1.0f, 1.0f, 0.08f));
	return Layer + 5;
}

int32 SAmplitudeGameView::PaintNotes(const FLayout& Layout, const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FAmplitudeSession& Session) const
{
	const Amp::FSimulation& Simulation = Session.GetSimulation();
	const std::vector<Amp::FNote>& Notes = Simulation.GetNotes();
	const double SongTimeMs = Session.GetSongTimeMs();
	const double ApproachMs = Simulation.GetParams().ApproachTimeMs;

	const float BaseWidth = FMath::Clamp(Layout.LaneWidth * 0.46f, 24.0f, 120.0f) * Layout.Zoom;
	const float BaseHeight = FMath::Clamp(Layout.LaneWidth * 0.46f * 0.36f, 12.0f, 34.0f) * Layout.Zoom;

	// Notes are sorted by time: start just before the oldest note that can still be on screen.
	const double OldestMs = SongTimeMs - Simulation.GetParams().GoodWindowMs - MissFadeMs;
	auto First = std::lower_bound(Notes.begin(), Notes.end(), OldestMs, [](const Amp::FNote& Note, double Value) { return Note.TimeMs < Value; });

	const Amp::FNote* PreviousChordNote = nullptr;
	float PreviousChordY = 0.0f;
	for (auto It = First; It != Notes.end() && It->TimeMs <= SongTimeMs + ApproachMs; ++It)
	{
		const Amp::FNote& Note = *It;
		float Alpha = 1.0f;
		bool bMissed = false;
		if (!Note.IsPending())
		{
			if (Note.Judgement != Amp::EJudgement::Miss || SongTimeMs - Note.ResolvedAtMs > MissFadeMs)
			{
				continue;
			}
			bMissed = true;
			Alpha = 1.0f - static_cast<float>((SongTimeMs - Note.ResolvedAtMs) / MissFadeMs);
		}

		const float Y = NoteY(Layout, Simulation, Note.TimeMs, SongTimeMs);
		if (Y < -0.05f || Y > 1.05f)
		{
			continue;
		}
		const FVector2D Center = Layout.ToScreen(FVector2D(Layout.LaneCenterX(Note.Lane), Y));
		const FLinearColor LaneColor = Tint(Layout, AmplitudeStyle::GetLaneColor(Note.Lane));
		const FLinearColor Color = bMissed ? FMath::Lerp(LaneColor, AmplitudeStyle::Miss, 0.7f) : LaneColor;
		const float Width = Note.Type == Amp::ENoteType::Stream ? BaseWidth * 0.8f : BaseWidth;
		const FVector2D Size(Width, BaseHeight);

		// Chord connector between the notes of a double/triple (they are adjacent in the sorted list).
		if (!bMissed && PreviousChordNote != nullptr && PreviousChordNote->ChordId == Note.ChordId && PreviousChordNote->IsPending())
		{
			const FVector2D From = Layout.ToScreen(FVector2D(Layout.LaneCenterX(PreviousChordNote->Lane), PreviousChordY));
			DrawLines(Out, Layer, Geometry, {From, Center}, FLinearColor(1.0f, 1.0f, 1.0f, 0.35f), 3.0f);
		}
		PreviousChordNote = &Note;
		PreviousChordY = Y;

		// Neon glow, body and a highlight stripe (spec 12.4).
		DrawCenteredBox(Out, Layer + 1, Geometry, Center, Size + FVector2D(22.0, 22.0), WithAlpha(Color, 0.07f * Alpha), AmplitudeStyle::RoundedBrush());
		DrawCenteredBox(Out, Layer + 1, Geometry, Center, Size + FVector2D(10.0, 10.0), WithAlpha(Color, 0.22f * Alpha), AmplitudeStyle::RoundedBrush());
		DrawCenteredBox(Out, Layer + 2, Geometry, Center, Size, WithAlpha(Color, Alpha), AmplitudeStyle::RoundedBrush());
		DrawCenteredBox(Out, Layer + 3, Geometry, Center - FVector2D(0.0, Size.Y * 0.22), FVector2D(Size.X * 0.8, FMath::Max(2.0, Size.Y * 0.16)),
			FLinearColor(1.0f, 1.0f, 1.0f, 0.45f * Alpha), AmplitudeStyle::RoundedBrush());
	}
	return Layer + 4;
}

int32 SAmplitudeGameView::PaintPowerups(const FLayout& Layout, const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FAmplitudeSession& Session) const
{
	const Amp::FSimulation& Simulation = Session.GetSimulation();
	const float Size = FMath::Clamp(Layout.LaneWidth * 0.2f, 18.0f, 56.0f) * Layout.Zoom;
	const FSlateFontInfo GlyphFont = AmplitudeStyle::Font(FMath::Max(8.0f, Size * 0.32f));

	for (const Amp::FFallingPowerup& Powerup : Simulation.GetPowerups())
	{
		const float Y = static_cast<float>(Simulation.GetPowerupY(Powerup));
		const FVector2D Center = Layout.ToScreen(FVector2D(Layout.LaneCenterX(Powerup.Lane), Y));
		const FLinearColor Color = Tint(Layout, AmplitudeStyle::GetPowerupColor(Powerup.Type));
		const float Pulse = 1.0f + 0.1f * FMath::Sin(static_cast<float>(Time) * 7.0f + Powerup.Id);
		const float Angle = static_cast<float>(Time) * 2.2f + Powerup.Id;
		// Fade out over the last second before despawning.
		const float Fade = FMath::Clamp((1.0f - Y) * 8.0f, 0.0f, 1.0f);

		DrawRotatedBox(Out, Layer, Geometry, Center, FVector2D(Size, Size) * 1.7f * Pulse, Angle, WithAlpha(Color, 0.18f * Fade), AmplitudeStyle::RoundedBrush());
		DrawRotatedBox(Out, Layer + 1, Geometry, Center, FVector2D(Size, Size) * Pulse, Angle, WithAlpha(Color, Fade), AmplitudeStyle::RoundedBrush());
		DrawRotatedBox(Out, Layer + 2, Geometry, Center, FVector2D(Size, Size) * 0.62f * Pulse, Angle, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f * Fade), AmplitudeStyle::RoundedBrush());
		PaintText(Out, Layer + 3, Geometry, AmplitudeStyle::GetPowerupGlyph(Powerup.Type), GlyphFont, Center, WithAlpha(FLinearColor::White, Fade), FVector2D(0.5, 0.5), false);
	}
	return Layer + 4;
}

int32 SAmplitudeGameView::PaintShip(const FLayout& Layout, const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FAmplitudeSession& Session) const
{
	const Amp::FSimulation& Simulation = Session.GetSimulation();
	const int32 Lane = Simulation.GetShipLane();
	const float EnergyFraction = static_cast<float>(Simulation.GetEnergy()) / static_cast<float>(FMath::Max(1, Simulation.GetRules().MaxEnergy));
	const FLinearColor LaneColor = Tint(Layout, AmplitudeStyle::GetLaneColor(Lane));
	const float Height = FMath::Clamp(static_cast<float>(Layout.Size.Y) * 0.035f, 22.0f, 64.0f) * Layout.Zoom;
	const float Width = Height * 0.95f;
	// Gentle bob on the beat (spec 12.3).
	const float Bob = FMath::Sin(static_cast<float>(Session.GetBeatPhase()) * 2.0f * UE_PI) * Height * 0.06f;
	const float ShipNorm = (Layout.ShipY - Layout.FieldTop) / Layout.FieldHeight;

	auto DrawShipAt = [&](float XNorm, float Alpha, int32 ShipLayer)
	{
		const FVector2D Center = Layout.ToScreen(FVector2D(XNorm, ShipNorm)) + FVector2D(0.0, Bob);
		const FVector2D Apex = Center + FVector2D(0.0, -Height * 0.55);
		const FVector2D LeftWing = Center + FVector2D(-Width * 0.5, Height * 0.45);
		const FVector2D RightWing = Center + FVector2D(Width * 0.5, Height * 0.45);
		const FVector2D Notch = Center + FVector2D(0.0, Height * 0.2);
		// Brightness follows the energy level.
		const FLinearColor Body = WithAlpha(FMath::Lerp(FLinearColor(0.35f, 0.35f, 0.42f), FLinearColor::White, 0.3f + 0.7f * EnergyFraction), Alpha);
		DrawFilledTriangle(Out, ShipLayer, Geometry, Apex, LeftWing, Notch, Body);
		DrawFilledTriangle(Out, ShipLayer, Geometry, Apex, Notch, RightWing, Body);
		DrawLines(Out, ShipLayer + 1, Geometry, {Apex, LeftWing, Notch, RightWing, Apex}, WithAlpha(LaneColor, Alpha), 2.5f);
		return Center;
	};

	// Energy trail when hopping lanes.
	if (ShipTrail > 0.0f && TrailFromLane >= 0 && TrailFromLane != Lane)
	{
		constexpr int32 Ghosts = 5;
		for (int32 Ghost = 0; Ghost < Ghosts; ++Ghost)
		{
			const float T = static_cast<float>(Ghost) / Ghosts;
			const float X = FMath::Lerp(Layout.LaneCenterX(TrailFromLane), Layout.LaneCenterX(Lane), T);
			DrawShipAt(X, ShipTrail * 0.18f * (T + 0.2f), Layer);
		}
	}

	const FVector2D ShipCenter = Layout.ToScreen(FVector2D(Layout.LaneCenterX(Lane), ShipNorm)) + FVector2D(0.0, Bob);
	// Engine glow in the lane colour; hotter during Fever.
	const FLinearColor GlowColor = Simulation.IsFeverActive() ? AmplitudeStyle::Fever : LaneColor;
	DrawCenteredBox(Out, Layer + 2, Geometry, ShipCenter + FVector2D(0.0, Height * 0.1), FVector2D(Width * 2.2, Height * 1.6), WithAlpha(GlowColor, 0.12f), AmplitudeStyle::RoundedBrush());
	DrawCenteredBox(Out, Layer + 2, Geometry, ShipCenter + FVector2D(0.0, Height * 0.45), FVector2D(Width * 0.35, Height * 0.35 * (0.8f + 0.4f * FMath::Frac(static_cast<float>(Time) * 9.0f))),
		WithAlpha(GlowColor, 0.8f), AmplitudeStyle::RoundedBrush());
	DrawShipAt(Layout.LaneCenterX(Lane), 1.0f, Layer + 3);

	if (Simulation.IsShieldActive())
	{
		const float Pulse = 0.5f + 0.5f * FMath::Sin(static_cast<float>(Time) * 5.0f);
		DrawCircle(Out, Layer + 5, Geometry, ShipCenter, Height * (0.95f + 0.06f * Pulse), WithAlpha(AmplitudeStyle::Shield, 0.45f + 0.35f * Pulse), 3.0f);
	}
	return Layer + 6;
}

int32 SAmplitudeGameView::PaintEffects(const FLayout& Layout, const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer) const
{
	for (const FRing& Ring : Rings)
	{
		const float T = Ring.Age / Ring.Life;
		const float Radius = Ring.MaxRadius * T * Layout.FieldWidth / Amp::NumLanes * 3.0f;
		DrawCircle(Out, Layer, Geometry, Layout.ToScreen(Ring.Position), Radius, WithAlpha(Ring.Color, 1.0f - T), 3.0f + 3.0f * (1.0f - T));
	}

	for (const FParticle& Particle : Particles)
	{
		const float T = Particle.Age / Particle.Life;
		const float Size = Particle.Size * (1.0f - 0.5f * T);
		DrawCenteredBox(Out, Layer + 1, Geometry, Layout.ToScreen(Particle.Position), FVector2D(Size, Size), WithAlpha(Tint(Layout, Particle.Color), 1.0f - T));
	}

	for (const FPopup& Popup : Popups)
	{
		const float T = Popup.Age / Popup.Life;
		const float Scale = T < 0.15f ? FMath::Lerp(1.4f, 1.0f, T / 0.15f) : 1.0f;
		const FVector2D Position = Layout.ToScreen(Popup.Position - FVector2D(0.0, Popup.Rise * T));
		const float FontSize = Popup.Size * Scale * FMath::Clamp(static_cast<float>(Layout.Size.Y) / 1080.0f, 0.7f, 1.6f);
		PaintText(Out, Layer + 2, Geometry, Popup.Text, AmplitudeStyle::Font(FontSize), Position, WithAlpha(Popup.Color, FMath::Min(1.0f, (1.0f - T) * 2.0f)), FVector2D(0.5, 0.5));
	}
	return Layer + 4;
}

int32 SAmplitudeGameView::PaintHud(const FLayout& Layout, const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FAmplitudeSession& Session) const
{
	const Amp::FSimulation& Simulation = Session.GetSimulation();
	const UAmplitudeUserSettings* Settings = UAmplitudeUserSettings::Get();
	const bool bShowCombo = Settings == nullptr || Settings->bShowCombo;
	const bool bShowEnergy = Settings == nullptr || Settings->bShowEnergy;
	const bool bShowStats = Settings != nullptr && Settings->bShowPerformanceStats;
	const float Width = static_cast<float>(Layout.Size.X);
	const float Height = static_cast<float>(Layout.Size.Y);
	const float Margin = Width * 0.03f;

	// ---- Top bar: score, timer, energy (spec 11.2.1)
	DrawBox(Out, Layer, Geometry, FVector2D::ZeroVector, FVector2D(Width, Layout.TopBar), FLinearColor(0.012f, 0.012f, 0.025f, 0.94f));
	DrawBox(Out, Layer, Geometry, FVector2D(0.0, Layout.TopBar - 2.0f), FVector2D(Width, 2.0), WithAlpha(AmplitudeStyle::Accent, 0.35f));

	const FSlateFontInfo BarFont = AmplitudeStyle::Font(Layout.TopBar * 0.36f);
	const FSlateFontInfo SmallFont = AmplitudeStyle::Font(Layout.TopBar * 0.28f);
	const float BarMid = Layout.TopBar * 0.5f;

	const FString ScoreText = FString::Printf(TEXT("SCORE  %s"), *AmplitudeStyle::FormatScore(Simulation.GetScore()).ToString());
	PaintText(Out, Layer + 1, Geometry, ScoreText, BarFont, FVector2D(Margin, BarMid), AmplitudeStyle::Text, FVector2D(0.0, 0.5));
	const double GlobalMultiplier = Simulation.GetGlobalMultiplier();
	if (GlobalMultiplier > 1.001)
	{
		const FVector2D ScoreSize = MeasureText(ScoreText, BarFont);
		PaintText(Out, Layer + 1, Geometry, FString::Printf(TEXT("x%.1f"), GlobalMultiplier), BarFont,
			FVector2D(Margin + ScoreSize.X + 16.0, BarMid), AmplitudeStyle::Perfect, FVector2D(0.0, 0.5));
	}

	const FString TimeText = FString::Printf(TEXT("TIME  %s / %s"),
		*AmplitudeStyle::FormatTime(FMath::Max(0.0, Session.GetSongTimeMs())).ToString(), *AmplitudeStyle::FormatTime(Session.GetDisplayDurationMs()).ToString());
	PaintText(Out, Layer + 1, Geometry, TimeText, BarFont, FVector2D(Width * 0.5f, BarMid), AmplitudeStyle::Text, FVector2D(0.5, 0.5));

	if (bShowEnergy)
	{
		const int32 Energy = Simulation.GetEnergy();
		const float Fraction = static_cast<float>(Energy) / static_cast<float>(FMath::Max(1, Simulation.GetRules().MaxEnergy));
		const bool bLow = Simulation.IsEnergyLow();
		const float Flash = bLow ? 0.5f + 0.5f * FMath::Sin(static_cast<float>(Time) * 12.0f) : 1.0f;
		const FLinearColor EnergyColor = WithAlpha(AmplitudeStyle::GetEnergyColor(Fraction), bLow ? 0.35f + 0.65f * Flash : 1.0f);
		const FVector2D BarSize(Width * 0.18f, Layout.TopBar * 0.3f);
		const FVector2D BarPosition(Width - Margin - BarSize.X, BarMid - BarSize.Y * 0.5f);
		DrawBox(Out, Layer + 1, Geometry, BarPosition, BarSize, FLinearColor(1.0f, 1.0f, 1.0f, 0.08f), AmplitudeStyle::RoundedBrush());
		DrawBox(Out, Layer + 2, Geometry, BarPosition, FVector2D(BarSize.X * Fraction, BarSize.Y), EnergyColor, AmplitudeStyle::RoundedBrush());
		PaintText(Out, Layer + 1, Geometry, FString::Printf(TEXT("ENERGY  %d/%d"), Energy, Simulation.GetRules().MaxEnergy), BarFont,
			FVector2D(BarPosition.X - 16.0f, BarMid), bLow ? EnergyColor : AmplitudeStyle::Text, FVector2D(1.0, 0.5));
	}

	// ---- Bottom bar: combo, lane status, powerups, debug (spec 11.2.3 / 11.2.4)
	const float BottomTop = Height - Layout.BottomBar;
	DrawBox(Out, Layer, Geometry, FVector2D(0.0, BottomTop), FVector2D(Width, Layout.BottomBar), FLinearColor(0.012f, 0.012f, 0.025f, 0.94f));
	DrawBox(Out, Layer, Geometry, FVector2D(0.0, BottomTop), FVector2D(Width, 2.0), WithAlpha(AmplitudeStyle::Accent, 0.35f));

	const FSlateFontInfo CellFont = AmplitudeStyle::Font(FMath::Clamp(Layout.LaneWidth * 0.07f, 10.0f, 17.0f));
	const float RowOne = BottomTop + Layout.BottomBar * 0.28f;
	const float RowTwo = BottomTop + Layout.BottomBar * 0.68f;
	for (int32 Lane = 0; Lane < Amp::NumLanes; ++Lane)
	{
		const Amp::FLaneState& State = Simulation.GetLane(Lane);
		const float CenterX = Layout.FieldLeft + Layout.LaneWidth * (Lane + 0.5f);
		const FLinearColor LaneColor = AmplitudeStyle::GetLaneColor(Lane);
		FString Status;
		FLinearColor StatusColor = AmplitudeStyle::TextDim;
		if (State.bCaptured)
		{
			Status = FString::Printf(TEXT("CAPTURED (%s)"), *Seconds(Simulation.GetCaptureRemainingMs(Lane)));
			StatusColor = LaneColor;
		}
		else if (State.bMuted)
		{
			Status = FString::Printf(TEXT("MUTED  x%d"), State.ConsecutiveMisses);
			StatusColor = AmplitudeStyle::Miss;
		}
		else if (State.ConsecutiveMisses > 0)
		{
			Status = FString::Printf(TEXT("MISS x%d"), State.ConsecutiveMisses);
			StatusColor = AmplitudeStyle::Fever;
		}
		else
		{
			Status = FString::Printf(TEXT("%d HITS"), State.Hits);
		}
		if (bShowCombo && State.Combo > 0)
		{
			Status += FString::Printf(TEXT("  x%d"), State.Combo);
		}
		PaintText(Out, Layer + 1, Geometry, FString::Printf(TEXT("LANE %d"), Lane + 1), CellFont, FVector2D(CenterX, RowOne - 9.0f), WithAlpha(LaneColor, 0.8f), FVector2D(0.5, 1.0));
		PaintText(Out, Layer + 1, Geometry, Status, CellFont, FVector2D(CenterX, RowOne - 7.0f), StatusColor, FVector2D(0.5, 0.0));
	}

	const FSlateFontInfo RowFont = AmplitudeStyle::Font(FMath::Clamp(Height * 0.021f, 11.0f, 22.0f));
	float Cursor = Margin;
	if (bShowCombo)
	{
		const int32 ShipLane = Simulation.GetShipLane();
		const FString ComboText = FString::Printf(TEXT("COMBO: %d (%.1fx)"), Simulation.GetLane(ShipLane).Combo, Simulation.GetLaneComboMultiplier(ShipLane));
		PaintText(Out, Layer + 1, Geometry, ComboText, RowFont, FVector2D(Cursor, RowTwo), AmplitudeStyle::GetLaneColor(ShipLane), FVector2D(0.0, 0.5));
		Cursor += static_cast<float>(MeasureText(ComboText, RowFont).X) + 36.0f;
	}

	TArray<TPair<FString, FLinearColor>> Effects;
	const Amp::FActiveEffects& Active = Simulation.GetEffects();
	if (!Active.Score2xRemainingMs.empty())
	{
		const double Longest = *std::max_element(Active.Score2xRemainingMs.begin(), Active.Score2xRemainingMs.end());
		const int32 Stacks = Simulation.GetScore2xStacks();
		Effects.Emplace(Stacks > 1 ? FString::Printf(TEXT("SCORE %dX (%s)"), 1 << Stacks, *Seconds(Longest)) : FString::Printf(TEXT("SCORE 2X (%s)"), *Seconds(Longest)),
			AmplitudeStyle::GetPowerupColor(Amp::EPowerupType::Score2x));
	}
	if (Simulation.IsFeverActive())
	{
		Effects.Emplace(FString::Printf(TEXT("FEVER MODE %.1fx (%s)"), Active.FeverMultiplier, *Seconds(Active.FeverRemainingMs)), AmplitudeStyle::GetPowerupColor(Amp::EPowerupType::Fever));
	}
	if (Simulation.IsShieldActive())
	{
		Effects.Emplace(FString::Printf(TEXT("SHIELD (%s)"), *Seconds(Active.ShieldRemainingMs)), AmplitudeStyle::GetPowerupColor(Amp::EPowerupType::Shield));
	}
	if (Simulation.IsSlowMotionActive())
	{
		Effects.Emplace(FString::Printf(TEXT("SLOW-MO (%s)"), *Seconds(Active.SlowMotionRemainingMs)), AmplitudeStyle::GetPowerupColor(Amp::EPowerupType::SlowMotion));
	}
	if (Simulation.IsLaneCleanerArmed())
	{
		Effects.Emplace(FString::Printf(TEXT("LANE CLEANER: PICK A LANE (%s)"), *Seconds(Active.LaneCleanerRemainingMs)), AmplitudeStyle::GetPowerupColor(Amp::EPowerupType::LaneCleaner));
	}
	if (Effects.Num() > 0)
	{
		PaintText(Out, Layer + 1, Geometry, TEXT("POWERUP:"), RowFont, FVector2D(Cursor, RowTwo), AmplitudeStyle::TextDim, FVector2D(0.0, 0.5));
		Cursor += static_cast<float>(MeasureText(TEXT("POWERUP:"), RowFont).X) + 12.0f;
		for (const TPair<FString, FLinearColor>& Effect : Effects)
		{
			PaintText(Out, Layer + 1, Geometry, Effect.Key, RowFont, FVector2D(Cursor, RowTwo), Effect.Value, FVector2D(0.0, 0.5));
			Cursor += static_cast<float>(MeasureText(Effect.Key, RowFont).X) + 24.0f;
		}
	}

	FString RightText;
	if (!Session.HasAudio())
	{
		RightText = TEXT("NO AUDIO  ");
	}
	if (bShowStats)
	{
		RightText += FString::Printf(TEXT("FPS: %.0f   SYNC: %+.1f ms   RESYNCS: %d   INPUT: %.0f ms"),
			SmoothedFps, Session.GetAudioDriftMs(), Session.GetResyncCount(), Session.GetInputLatencyMs());
	}
	if (!RightText.IsEmpty())
	{
		PaintText(Out, Layer + 1, Geometry, RightText, AmplitudeStyle::Font(FMath::Clamp(Height * 0.016f, 10.0f, 16.0f)),
			FVector2D(Width - Margin, RowTwo), AmplitudeStyle::TextDim, FVector2D(1.0, 0.5));
	}
	return Layer + 3;
}

int32 SAmplitudeGameView::PaintCenterMessages(const FLayout& Layout, const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FAmplitudeSession& Session) const
{
	const Amp::FSimulation& Simulation = Session.GetSimulation();
	const float Height = static_cast<float>(Layout.Size.Y);
	const FVector2D Center(Layout.Size.X * 0.5, Layout.FieldTop + Layout.FieldHeight * 0.42f);
	const FSlateFontInfo HugeFont = AmplitudeStyle::Font(FMath::Clamp(Height * 0.09f, 36.0f, 120.0f));
	const FSlateFontInfo BigFont = AmplitudeStyle::Font(FMath::Clamp(Height * 0.045f, 20.0f, 64.0f));
	const FSlateFontInfo MediumFont = AmplitudeStyle::Font(FMath::Clamp(Height * 0.024f, 14.0f, 30.0f));

	const AAmplitudeDirector* Owner = Director.Get();
	const double ResumeCountdown = Owner != nullptr ? Owner->GetResumeCountdownSeconds() : 0.0;
	const double SongTimeMs = Session.GetSongTimeMs();

	if (ResumeCountdown > 0.0)
	{
		const int32 Step = FMath::CeilToInt32(ResumeCountdown / 0.4);
		DrawBox(Out, Layer, Geometry, FVector2D::ZeroVector, Layout.Size, FLinearColor(0.0f, 0.0f, 0.0f, 0.35f));
		PaintText(Out, Layer + 1, Geometry, FString::FromInt(Step), HugeFont, Center, AmplitudeStyle::Accent, FVector2D(0.5, 0.5));
	}
	else if (SongTimeMs < 0.0)
	{
		// Lead-in: song title, then a 3-2-1 count into the first beat.
		const FAmplitudeSongDefinition& Song = Session.GetSong();
		const FString Title = Song.Artist.IsEmpty() ? Song.Title : FString::Printf(TEXT("%s - %s"), *Song.Title, *Song.Artist);
		PaintText(Out, Layer + 1, Geometry, Title, BigFont, Center - FVector2D(0.0, Height * 0.1f), AmplitudeStyle::Text, FVector2D(0.5, 0.5));
		PaintText(Out, Layer + 1, Geometry, FString(ANSI_TO_TCHAR(Amp::GetDifficultyName(Session.GetDifficulty()))).ToUpper(), MediumFont,
			Center - FVector2D(0.0, Height * 0.05f), AmplitudeStyle::GetDifficultyColor(Session.GetDifficulty()), FVector2D(0.5, 0.5));
		const int32 Count = FMath::CeilToInt32(-SongTimeMs / 1000.0);
		const FString CountText = Count <= 3 ? FString::FromInt(Count) : FString(TEXT("GET READY"));
		PaintText(Out, Layer + 1, Geometry, CountText, Count <= 3 ? HugeFont : BigFont, Center + FVector2D(0.0, Height * 0.05f), AmplitudeStyle::Accent, FVector2D(0.5, 0.5));
	}
	else if (SongTimeMs < 600.0 && !Simulation.IsFinished())
	{
		PaintText(Out, Layer + 1, Geometry, TEXT("GO!"), HugeFont, Center, WithAlpha(AmplitudeStyle::Accent, 1.0f - static_cast<float>(SongTimeMs / 600.0)), FVector2D(0.5, 0.5));
	}

	for (const FBanner& Banner : Banners)
	{
		const float T = Banner.Age / Banner.Life;
		const float Alpha = FMath::Min(1.0f, FMath::Min(Banner.Age * 8.0f, (1.0f - T) * 4.0f));
		const float Scale = Banner.Age < 0.12f ? FMath::Lerp(1.3f, 1.0f, Banner.Age / 0.12f) : 1.0f;
		const FSlateFontInfo Font = AmplitudeStyle::Font(FMath::Clamp(Height * 0.05f, 22.0f, 72.0f) * Scale);
		PaintText(Out, Layer + 2, Geometry, Banner.Text, Font, FVector2D(Center.X, Layout.FieldTop + Layout.FieldHeight * 0.22f), WithAlpha(Banner.Color, Alpha), FVector2D(0.5, 0.5));
	}

	if (Simulation.IsFinished())
	{
		DrawBox(Out, Layer, Geometry, FVector2D::ZeroVector, Layout.Size, FLinearColor(0.0f, 0.0f, 0.0f, 0.3f));
	}
	return Layer + 4;
}
