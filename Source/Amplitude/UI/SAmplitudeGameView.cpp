#include "UI/SAmplitudeGameView.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Core/AmpRules.h"
#include "Data/AmplitudeSongLibrary.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Game/AmplitudeDirector.h"
#include "Game/AmplitudeSession.h"
#include "Game/AmplitudeUserSettings.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Stage/AmplitudeStage.h"
#include "UI/AmplitudeStyle.h"

#include <algorithm>

namespace
{
	const FLinearColor Ink(0.03f, 0.035f, 0.07f, 1.0f);

	FLinearColor WithAlpha(const FLinearColor& Color, float Alpha)
	{
		FLinearColor Result = Color;
		Result.A = Alpha;
		return Result;
	}

	const FSlateBrush* OutlineBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::Transparent, 8.0f, FLinearColor::White, 2.0f);
		return &Brush;
	}

	const FSlateBrush* PillBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 10.0f);
		return &Brush;
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
			FSlateDrawElement::MakeText(Out, Layer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Position + FVector2D(0.0, 2.0))), Text, Font, ESlateDrawEffect::None, FLinearColor(0.0f, 0.0f, 0.05f, Color.A * 0.45f));
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

	/** A rounded "keycap" with a key name inside; returns its width. */
	float PaintKeycap(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2D& Position, const FString& Label, const FLinearColor& Tint, float Scale, float Alpha)
	{
		const FSlateFontInfo Font = AmplitudeStyle::Font(14.0f * Scale);
		const FVector2D TextSize = MeasureText(Label, Font);
		const FVector2D Size(FMath::Max(40.0 * Scale, TextSize.X + 20.0 * Scale), 40.0 * Scale);
		DrawBox(Out, Layer, Geometry, Position, Size, WithAlpha(Ink, 0.72f * Alpha), PillBrush());
		DrawBox(Out, Layer + 1, Geometry, Position, Size, WithAlpha(Tint, 0.9f * Alpha), OutlineBrush());
		PaintText(Out, Layer + 1, Geometry, Label, Font, Position + Size * 0.5, WithAlpha(AmplitudeStyle::Text, Alpha), FVector2D(0.5, 0.5), false);
		return static_cast<float>(Size.X);
	}
}

void SAmplitudeGameView::Construct(const FArguments& InArgs)
{
	Director = InArgs._Director;
	SetCanTick(true);
	// Everything animates every frame; never cache this widget's draw elements.
	ForceVolatile(true);
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

FVector SAmplitudeGameView::GetHitPoint(int32 Lane, int32 Column) const
{
	const AAmplitudeDirector* Owner = Director.Get();
	const AAmplitudeStage* Stage = Owner != nullptr ? Owner->GetStage() : nullptr;
	return Stage != nullptr ? Stage->GetHitPoint(Lane, Column) : FVector::ZeroVector;
}

FVector SAmplitudeGameView::GetShipLocation() const
{
	const AAmplitudeDirector* Owner = Director.Get();
	const AAmplitudeStage* Stage = Owner != nullptr ? Owner->GetStage() : nullptr;
	return Stage != nullptr ? Stage->GetShipLocation() : FVector::ZeroVector;
}

bool SAmplitudeGameView::ProjectToLocal(const FVector& WorldLocation, const FVector2D& LocalSize, FVector2D& OutLocal) const
{
	const AAmplitudeDirector* Owner = Director.Get();
	const AAmplitudeStage* Stage = Owner != nullptr ? Owner->GetStage() : nullptr;
	FVector2D Fraction;
	if (Stage == nullptr || !Stage->ProjectToViewport(WorldLocation, Fraction))
	{
		return false;
	}
	OutLocal = Fraction * LocalSize;
	return true;
}

void SAmplitudeGameView::ResetEffects()
{
	Popups.Reset();
	Banners.Reset();
	FeverFlash = 0.0f;
	ScorePulse = 0.0f;
}

void SAmplitudeGameView::AddPopup(const FString& Text, const FLinearColor& Color, const FVector& WorldLocation, float Size, float Life, const FVector2D& Offset)
{
	FPopup Popup;
	Popup.Text = Text;
	Popup.Color = Color;
	Popup.WorldLocation = WorldLocation;
	Popup.Offset = Offset;
	Popup.Size = Size;
	Popup.Life = Life;
	Popups.Add(Popup);
	constexpr int32 MaxPopups = 40;
	if (Popups.Num() > MaxPopups)
	{
		Popups.RemoveAt(0);
	}
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

void SAmplitudeGameView::HandleSimEvent(const Amp::FEvent& Event)
{
	const FAmplitudeSession* Session = GetSession();
	const int32 Lane = FMath::Clamp(Event.Lane, 0, Amp::NumLanes - 1);
	const int32 Column = Event.Column >= 0 ? Event.Column : 1;
	const FLinearColor LaneColor = AmplitudeStyle::GetLaneColor(Lane);

	switch (Event.Type)
	{
	case Amp::EEventType::NoteHit:
	{
		if (Event.bAuto)
		{
			break;
		}
		const bool bPerfect = Event.Judgement == Amp::EJudgement::Perfect;
		const FLinearColor Color = bPerfect ? AmplitudeStyle::Perfect : AmplitudeStyle::Good;
		const FVector Point = GetHitPoint(Lane, Column);
		AddPopup(bPerfect ? TEXT("PERFECT") : TEXT("GOOD"), Color, Point, 22.0f, 0.55f, FVector2D(0.0, -34.0));
		AddPopup(FString::Printf(TEXT("+%lld"), static_cast<long long>(Event.Points)), WithAlpha(AmplitudeStyle::Text, 0.9f), Point, 15.0f, 0.5f, FVector2D(0.0, -8.0));
		ScorePulse = 1.0f;
		if (Session != nullptr)
		{
			const Amp::FDifficultyParams& Params = Session->GetSimulation().GetParams();
			const double Now = Amp::GetComboMultiplier(Params, Event.Count);
			if (Now > Amp::GetComboMultiplier(Params, Event.Count - 1))
			{
				AddPopup(FString::Printf(TEXT("COMBO x%.1f"), Now), LaneColor, GetHitPoint(Lane, 1), 24.0f, 0.9f, FVector2D(0.0, -80.0));
			}
		}
		break;
	}

	case Amp::EEventType::NoteMissed:
		AddPopup(TEXT("MISS"), AmplitudeStyle::Miss, GetHitPoint(Lane, Column), 22.0f, 0.6f, FVector2D(0.0, -34.0));
		break;

	case Amp::EEventType::LaneCaptured:
		AddPopup(TEXT("CAPTURED!"), LaneColor, GetHitPoint(Lane, 1), 32.0f, 1.2f, FVector2D(0.0, -120.0));
		AddPopup(FString::Printf(TEXT("-%d ENERGY"), Event.Count), WithAlpha(AmplitudeStyle::TextDim, 0.95f), GetHitPoint(Lane, 1), 14.0f, 1.2f, FVector2D(0.0, -88.0));
		break;

	case Amp::EEventType::CaptureExpired:
		AddPopup(TEXT("RELEASED"), WithAlpha(LaneColor, 0.9f), GetHitPoint(Lane, 1), 18.0f, 1.0f, FVector2D(0.0, -100.0));
		break;

	case Amp::EEventType::LaneMuted:
		AddPopup(TEXT("MUTED"), AmplitudeStyle::Miss, GetHitPoint(Lane, 1), 26.0f, 1.0f, FVector2D(0.0, -110.0));
		break;

	case Amp::EEventType::LaneUnmuted:
		AddPopup(TEXT("BACK IN THE MIX"), LaneColor, GetHitPoint(Lane, 1), 18.0f, 1.0f, FVector2D(0.0, -110.0));
		break;

	case Amp::EEventType::PowerupCollected:
	{
		const FLinearColor Color = AmplitudeStyle::GetPowerupColor(Event.Powerup);
		const bool bCleaner = Event.Powerup == Amp::EPowerupType::LaneCleaner;
		AddBanner(bCleaner ? FString(TEXT("LANE CLEANER - PRESS A GEM BUTTON TO CLEAR YOUR LANE")) : GetPowerupStatusName(Event.Powerup) + TEXT("!"), Color, bCleaner ? 3.0f : 1.4f);
		AddPopup(FString::Printf(TEXT("+%lld"), static_cast<long long>(Event.Points)), Color, GetShipLocation(), 20.0f, 0.8f, FVector2D(0.0, -60.0));
		if (Event.Powerup == Amp::EPowerupType::Fever)
		{
			FeverFlash = 1.0f;
		}
		break;
	}

	case Amp::EEventType::LaneCleared:
		AddPopup(FString::Printf(TEXT("CLEARED %d"), Event.Count), AmplitudeStyle::GetPowerupColor(Amp::EPowerupType::LaneCleaner), GetHitPoint(Lane, 1), 24.0f, 1.0f, FVector2D(0.0, -110.0));
		Banners.Reset();
		break;

	case Amp::EEventType::ShieldAbsorbedMiss:
		AddPopup(TEXT("SHIELDED"), AmplitudeStyle::Shield, GetShipLocation(), 20.0f, 0.8f, FVector2D(0.0, -70.0));
		break;

	case Amp::EEventType::FeverBroken:
		AddBanner(TEXT("FEVER BROKEN"), WithAlpha(AmplitudeStyle::Fever, 0.85f), 1.0f);
		break;

	case Amp::EEventType::AutoCaptureFailed:
		AddBanner(TEXT("AUTO-CAPTURE FIZZLED - NOT ENOUGH ENERGY"), AmplitudeStyle::TextDim, 1.4f);
		break;

	case Amp::EEventType::EnergyLow:
		AddBanner(TEXT("ENERGY LOW!"), AmplitudeStyle::EnergyLow, 1.2f);
		break;

	case Amp::EEventType::GameOver:
		AddBanner(TEXT("GAME OVER"), AmplitudeStyle::Miss, 10.0f);
		break;

	case Amp::EEventType::SongComplete:
		AddBanner(TEXT("SONG COMPLETE!"), AmplitudeStyle::Perfect, 10.0f);
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
	for (int32 Index = Popups.Num() - 1; Index >= 0; --Index)
	{
		Popups[Index].Age += Dt;
		if (Popups[Index].Age >= Popups[Index].Life)
		{
			Popups.RemoveAt(Index);
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
	FeverFlash = FMath::Max(0.0f, FeverFlash - Dt * 2.0f);
	ScorePulse = FMath::Max(0.0f, ScorePulse - Dt * 6.0f);
}

// ---------------------------------------------------------------------------------------------
// Painting

int32 SAmplitudeGameView::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FAmplitudeSession* Session = GetSession();
	if (Session == nullptr)
	{
		return LayerId;
	}

	const FVector2D Size(AllottedGeometry.GetLocalSize());
	const float Scale = FMath::Clamp(static_cast<float>(Size.Y) / 1080.0f, 0.6f, 1.8f);

	int32 Layer = LayerId;
	if (FeverFlash > 0.0f)
	{
		DrawBox(OutDrawElements, Layer, AllottedGeometry, FVector2D::ZeroVector, Size, WithAlpha(AmplitudeStyle::Fever, 0.18f * FeverFlash));
	}
	Layer = PaintPopups(AllottedGeometry, OutDrawElements, Layer + 1, Scale);
	Layer = PaintTopBar(AllottedGeometry, OutDrawElements, Layer, *Session, Scale);
	Layer = PaintLaneStrip(AllottedGeometry, OutDrawElements, Layer, *Session, Scale);
	Layer = PaintPowerups(AllottedGeometry, OutDrawElements, Layer, *Session, Scale);
	Layer = PaintPrompts(AllottedGeometry, OutDrawElements, Layer, *Session, Scale);
	return PaintCenterMessages(AllottedGeometry, OutDrawElements, Layer, *Session, Scale);
}

int32 SAmplitudeGameView::PaintTopBar(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FAmplitudeSession& Session, float Scale) const
{
	const Amp::FSimulation& Simulation = Session.GetSimulation();
	const UAmplitudeUserSettings* Settings = UAmplitudeUserSettings::Get();
	const FVector2D Size(Geometry.GetLocalSize());
	const float Margin = 30.0f * Scale;

	// ---- Score (top left) with the combo and powerup multipliers beside it
	PaintText(Out, Layer, Geometry, TEXT("SCORE"), AmplitudeStyle::Font(13.0f * Scale), FVector2D(Margin, Margin), AmplitudeStyle::TextDim);
	const FSlateFontInfo ScoreFont = AmplitudeStyle::Font(38.0f * Scale * (1.0f + 0.06f * ScorePulse));
	const FString ScoreText = AmplitudeStyle::FormatScore(Simulation.GetScore()).ToString();
	const FVector2D ScoreAnchor(Margin, Margin + 18.0f * Scale);
	PaintText(Out, Layer, Geometry, ScoreText, ScoreFont, ScoreAnchor, AmplitudeStyle::Text);
	const FVector2D ScoreSize = MeasureText(ScoreText, ScoreFont);

	float PillX = static_cast<float>(ScoreAnchor.X + ScoreSize.X) + 16.0f * Scale;
	const float PillY = static_cast<float>(ScoreAnchor.Y + ScoreSize.Y * 0.5) - 15.0f * Scale;
	auto PaintPill = [&](const FString& Text, const FLinearColor& Color)
	{
		const FSlateFontInfo Font = AmplitudeStyle::Font(15.0f * Scale);
		const FVector2D TextSize = MeasureText(Text, Font);
		const FVector2D PillSize(TextSize.X + 22.0f * Scale, 30.0f * Scale);
		DrawBox(Out, Layer + 2, Geometry, FVector2D(PillX, PillY), PillSize, Color, PillBrush());
		PaintText(Out, Layer + 3, Geometry, Text, Font, FVector2D(PillX, PillY) + PillSize * 0.5, Ink, FVector2D(0.5, 0.5), false);
		PillX += static_cast<float>(PillSize.X) + 8.0f * Scale;
	};
	const int32 ShipLane = Simulation.GetShipLane();
	const double ComboMultiplier = Simulation.GetLaneComboMultiplier(ShipLane);
	if (ComboMultiplier > 1.001)
	{
		PaintPill(FString::Printf(TEXT("COMBO x%.1f"), ComboMultiplier), AmplitudeStyle::GetLaneColor(ShipLane));
	}
	const double GlobalMultiplier = Simulation.GetGlobalMultiplier();
	if (GlobalMultiplier > 1.001)
	{
		PaintPill(FString::Printf(TEXT("BOOST x%.1f"), GlobalMultiplier), AmplitudeStyle::Perfect);
	}

	if (Settings != nullptr && Settings->bShowPerformanceStats)
	{
		PaintText(Out, Layer, Geometry, FString::Printf(TEXT("FPS %.0f   SYNC %+.1f ms   RESYNCS %d   INPUT %.0f ms"),
			SmoothedFps, Session.GetAudioDriftMs(), Session.GetResyncCount(), Session.GetInputLatencyMs()),
			AmplitudeStyle::Font(12.0f * Scale, false), FVector2D(Margin, ScoreAnchor.Y + ScoreSize.Y + 6.0f * Scale), AmplitudeStyle::TextDim);
	}

	// ---- Song and progress (top centre)
	const FAmplitudeSongDefinition& Song = Session.GetSong();
	const FString Title = Song.Artist.IsEmpty() ? Song.Title.ToUpper() : FString::Printf(TEXT("%s  -  %s"), *Song.Title.ToUpper(), *Song.Artist.ToUpper());
	PaintText(Out, Layer, Geometry, Title, AmplitudeStyle::Font(14.0f * Scale), FVector2D(Size.X * 0.5, Margin), AmplitudeStyle::Text, FVector2D(0.5, 0.0));
	const FVector2D BarSize(Size.X * 0.26, 6.0f * Scale);
	const FVector2D BarPosition(Size.X * 0.5 - BarSize.X * 0.5, Margin + 28.0f * Scale);
	const double Duration = FMath::Max(1.0, Session.GetDisplayDurationMs());
	const float Progress = static_cast<float>(FMath::Clamp(Session.GetSongTimeMs() / Duration, 0.0, 1.0));
	DrawBox(Out, Layer, Geometry, BarPosition, BarSize, FLinearColor(1.0f, 1.0f, 1.0f, 0.2f), PillBrush());
	DrawBox(Out, Layer + 1, Geometry, BarPosition, FVector2D(BarSize.X * Progress, BarSize.Y), AmplitudeStyle::Accent, PillBrush());
	const FString TimeText = FString::Printf(TEXT("%s / %s"),
		*AmplitudeStyle::FormatTime(FMath::Max(0.0, Session.GetSongTimeMs())).ToString(), *AmplitudeStyle::FormatTime(Duration).ToString());
	PaintText(Out, Layer, Geometry, TimeText, AmplitudeStyle::Font(12.0f * Scale, false), FVector2D(Size.X * 0.5, BarPosition.Y + 12.0f * Scale), AmplitudeStyle::TextDim, FVector2D(0.5, 0.0));

	// ---- Energy (top right)
	if (Settings == nullptr || Settings->bShowEnergy)
	{
		const int32 Energy = Simulation.GetEnergy();
		const float Fraction = static_cast<float>(Energy) / static_cast<float>(FMath::Max(1, Simulation.GetRules().MaxEnergy));
		const bool bLow = Simulation.IsEnergyLow();
		const float Flash = bLow ? 0.5f + 0.5f * FMath::Sin(static_cast<float>(Time) * 12.0f) : 1.0f;
		const FLinearColor EnergyColor = WithAlpha(AmplitudeStyle::GetEnergyColor(Fraction), bLow ? 0.4f + 0.6f * Flash : 1.0f);
		const FVector2D EnergySize(280.0f * Scale, 14.0f * Scale);
		const FVector2D EnergyPosition(Size.X - Margin - EnergySize.X, Margin + 26.0f * Scale);
		PaintText(Out, Layer, Geometry, TEXT("ENERGY"), AmplitudeStyle::Font(13.0f * Scale), FVector2D(EnergyPosition.X, Margin), AmplitudeStyle::TextDim);
		PaintText(Out, Layer, Geometry, FString::FromInt(Energy), AmplitudeStyle::Font(16.0f * Scale), FVector2D(Size.X - Margin, Margin - 2.0f * Scale),
			bLow ? EnergyColor : AmplitudeStyle::Text, FVector2D(1.0, 0.0));
		DrawBox(Out, Layer, Geometry, EnergyPosition, EnergySize, FLinearColor(1.0f, 1.0f, 1.0f, 0.2f), PillBrush());
		DrawBox(Out, Layer + 1, Geometry, EnergyPosition, FVector2D(EnergySize.X * Fraction, EnergySize.Y), EnergyColor, PillBrush());
		if (!Session.HasAudio())
		{
			PaintText(Out, Layer, Geometry, TEXT("NO AUDIO"), AmplitudeStyle::Font(12.0f * Scale), FVector2D(Size.X - Margin, EnergyPosition.Y + 24.0f * Scale), AmplitudeStyle::TextDim, FVector2D(1.0, 0.0));
		}
	}
	return Layer + 4;
}

int32 SAmplitudeGameView::PaintLaneStrip(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FAmplitudeSession& Session, float Scale) const
{
	const Amp::FSimulation& Simulation = Session.GetSimulation();
	const UAmplitudeUserSettings* Settings = UAmplitudeUserSettings::Get();
	const bool bShowCombo = Settings == nullptr || Settings->bShowCombo;
	const FVector2D Size(Geometry.GetLocalSize());
	const float Margin = 30.0f * Scale;
	const float PillWidth = 136.0f * Scale;
	const float PillHeight = 48.0f * Scale;
	const float Gap = 10.0f * Scale;
	const float TotalWidth = PillWidth * Amp::NumLanes + Gap * (Amp::NumLanes - 1);
	const float Left = static_cast<float>(Size.X) * 0.5f - TotalWidth * 0.5f;
	const float Top = static_cast<float>(Size.Y) - Margin - PillHeight - 12.0f * Scale;
	const int32 ShipLane = Simulation.GetShipLane();
	const int32 CaptureStreak = FMath::Max(1, Simulation.GetRules().CaptureStreak);

	for (int32 Lane = 0; Lane < Amp::NumLanes; ++Lane)
	{
		const Amp::FLaneState& State = Simulation.GetLane(Lane);
		const FLinearColor LaneColor = AmplitudeStyle::GetLaneColor(Lane);
		const bool bShip = Lane == ShipLane;
		const FVector2D Position(Left + Lane * (PillWidth + Gap), Top - (bShip ? 6.0f * Scale : 0.0f));
		const FVector2D PillSize(PillWidth, PillHeight);

		FLinearColor Fill = WithAlpha(FMath::Lerp(Ink, LaneColor, 0.35f), 0.78f);
		if (State.bCaptured)
		{
			Fill = WithAlpha(LaneColor, 0.92f);
		}
		else if (State.bMuted)
		{
			Fill = FLinearColor(0.2f, 0.2f, 0.24f, 0.8f);
		}
		DrawBox(Out, Layer, Geometry, Position, PillSize, Fill, PillBrush());
		if (bShip)
		{
			DrawBox(Out, Layer + 1, Geometry, Position, PillSize, FLinearColor::White, OutlineBrush());
			DrawBox(Out, Layer + 1, Geometry, Position + FVector2D(PillWidth * 0.5f - 14.0f * Scale, -10.0f * Scale), FVector2D(28.0f * Scale, 4.0f * Scale), FLinearColor::White, PillBrush());
		}

		const FLinearColor TextColor = State.bCaptured ? Ink : AmplitudeStyle::Text;
		PaintText(Out, Layer + 2, Geometry, AmplitudeStyle::GetLaneLabel(Lane).ToString(), AmplitudeStyle::Font(14.0f * Scale),
			Position + FVector2D(PillWidth * 0.5f, 8.0f * Scale), TextColor, FVector2D(0.5, 0.0), !State.bCaptured);

		FString Status;
		if (State.bCaptured)
		{
			Status = FString::Printf(TEXT("AUTO  %s"), *Seconds(Simulation.GetCaptureRemainingMs(Lane)));
		}
		else if (State.bMuted)
		{
			Status = TEXT("MUTED");
		}
		else if (bShowCombo && State.Combo > 0)
		{
			Status = FString::Printf(TEXT("x%d  (%.1fx)"), State.Combo, Simulation.GetLaneComboMultiplier(Lane));
		}
		PaintText(Out, Layer + 2, Geometry, Status, AmplitudeStyle::Font(11.0f * Scale, false),
			Position + FVector2D(PillWidth * 0.5f, 28.0f * Scale), State.bCaptured ? Ink : (State.bMuted ? AmplitudeStyle::Miss : AmplitudeStyle::TextDim), FVector2D(0.5, 0.0), false);

		// Progress towards capture, or the time left on it.
		const FVector2D TrackPosition = Position + FVector2D(10.0f * Scale, PillHeight + 5.0f * Scale);
		const FVector2D TrackSize(PillWidth - 20.0f * Scale, 4.0f * Scale);
		const float Fraction = State.bCaptured
			? static_cast<float>(Simulation.GetCaptureRemainingMs(Lane) / FMath::Max(1.0, Simulation.GetRules().CaptureDurationMs))
			: static_cast<float>(State.CaptureStreak) / static_cast<float>(CaptureStreak);
		DrawBox(Out, Layer, Geometry, TrackPosition, TrackSize, FLinearColor(1.0f, 1.0f, 1.0f, 0.18f), PillBrush());
		DrawBox(Out, Layer + 1, Geometry, TrackPosition, FVector2D(TrackSize.X * FMath::Clamp(Fraction, 0.0f, 1.0f), TrackSize.Y), LaneColor, PillBrush());
	}
	return Layer + 4;
}

int32 SAmplitudeGameView::PaintPowerups(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FAmplitudeSession& Session, float Scale) const
{
	const Amp::FSimulation& Simulation = Session.GetSimulation();
	const Amp::FGameRules& Rules = Simulation.GetRules();
	const Amp::FActiveEffects& Active = Simulation.GetEffects();
	const FVector2D Size(Geometry.GetLocalSize());
	const float Margin = 30.0f * Scale;

	struct FChip
	{
		FString Text;
		FLinearColor Color;
		float Fraction;
	};
	TArray<FChip> Chips;
	auto Fraction = [](double Remaining, double Total)
	{
		return static_cast<float>(FMath::Clamp(Remaining / FMath::Max(1.0, Total), 0.0, 1.0));
	};

	if (!Active.Score2xRemainingMs.empty())
	{
		const double Longest = *std::max_element(Active.Score2xRemainingMs.begin(), Active.Score2xRemainingMs.end());
		const int32 Stacks = Simulation.GetScore2xStacks();
		Chips.Add({FString::Printf(TEXT("SCORE %dX  %s"), 1 << Stacks, *Seconds(Longest)), AmplitudeStyle::GetPowerupColor(Amp::EPowerupType::Score2x), Fraction(Longest, Rules.Score2xDurationMs)});
	}
	if (Simulation.IsFeverActive())
	{
		Chips.Add({FString::Printf(TEXT("FEVER %.1fx  %s"), Active.FeverMultiplier, *Seconds(Active.FeverRemainingMs)), AmplitudeStyle::GetPowerupColor(Amp::EPowerupType::Fever), Fraction(Active.FeverRemainingMs, Rules.FeverDurationMs)});
	}
	if (Simulation.IsShieldActive())
	{
		Chips.Add({FString::Printf(TEXT("SHIELD  %s"), *Seconds(Active.ShieldRemainingMs)), AmplitudeStyle::GetPowerupColor(Amp::EPowerupType::Shield), Fraction(Active.ShieldRemainingMs, Rules.ShieldDurationMs)});
	}
	if (Simulation.IsSlowMotionActive())
	{
		Chips.Add({FString::Printf(TEXT("SLOW-MO  %s"), *Seconds(Active.SlowMotionRemainingMs)), AmplitudeStyle::GetPowerupColor(Amp::EPowerupType::SlowMotion), Fraction(Active.SlowMotionRemainingMs, Rules.SlowMotionDurationMs)});
	}
	if (Simulation.IsLaneCleanerArmed())
	{
		Chips.Add({FString::Printf(TEXT("LANE CLEANER  %s"), *Seconds(Active.LaneCleanerRemainingMs)), AmplitudeStyle::GetPowerupColor(Amp::EPowerupType::LaneCleaner), Fraction(Active.LaneCleanerRemainingMs, Rules.LaneCleanerSelectTimeoutMs)});
	}

	// Stacked upwards from the bottom-left corner.
	const FSlateFontInfo Font = AmplitudeStyle::Font(14.0f * Scale);
	float Bottom = static_cast<float>(Size.Y) - Margin;
	for (const FChip& Chip : Chips)
	{
		const FVector2D TextSize = MeasureText(Chip.Text, Font);
		const FVector2D ChipSize(TextSize.X + 28.0f * Scale, 34.0f * Scale);
		const FVector2D Position(Margin, Bottom - ChipSize.Y);
		DrawBox(Out, Layer, Geometry, Position, ChipSize, WithAlpha(Chip.Color, 0.92f), PillBrush());
		DrawBox(Out, Layer + 1, Geometry, Position + FVector2D(10.0f * Scale, ChipSize.Y - 7.0f * Scale), FVector2D((ChipSize.X - 20.0f * Scale) * Chip.Fraction, 3.0f * Scale), WithAlpha(Ink, 0.6f), PillBrush());
		PaintText(Out, Layer + 1, Geometry, Chip.Text, Font, Position + FVector2D(ChipSize.X * 0.5, ChipSize.Y * 0.45), Ink, FVector2D(0.5, 0.5), false);
		Bottom -= static_cast<float>(ChipSize.Y) + 8.0f * Scale;
	}
	return Layer + 3;
}

int32 SAmplitudeGameView::PaintPrompts(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FAmplitudeSession& Session, float Scale) const
{
	const UAmplitudeUserSettings* Settings = UAmplitudeUserSettings::Get();
	const AAmplitudeDirector* Owner = Director.Get();
	if (Settings == nullptr || Owner == nullptr)
	{
		return Layer;
	}
	// Full strength while the player learns the song's opening, then they step back.
	const double SongTimeMs = Session.GetSongTimeMs();
	const float Alpha = SongTimeMs < 12000.0 ? 1.0f : 0.45f;
	const bool bGamepad = Owner->IsUsingGamepad();
	const FAmplitudeControlProfile& Profile = Settings->GetActiveProfile();
	const FVector2D Size(Geometry.GetLocalSize());
	const float Margin = 30.0f * Scale;
	const FSlateFontInfo LabelFont = AmplitudeStyle::Font(11.0f * Scale);

	auto KeyText = [&Profile, bGamepad](int32 Action)
	{
		return AmplitudeStyle::GetKeyLabel(Profile.GetDisplayKey(Action, bGamepad)).ToString();
	};

	// Measure right to left so the block hugs the bottom-right corner.
	const FString Labels[5] = {KeyText(AmplitudeControls::MoveLeft), KeyText(AmplitudeControls::MoveRight),
		KeyText(AmplitudeControls::GemLeft), KeyText(AmplitudeControls::GemMiddle), KeyText(AmplitudeControls::GemRight)};
	const FSlateFontInfo KeyFont = AmplitudeStyle::Font(14.0f * Scale);
	float Width = 0.0f;
	float Widths[5];
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Widths[Index] = FMath::Max(40.0f * Scale, static_cast<float>(MeasureText(Labels[Index], KeyFont).X) + 20.0f * Scale);
		Width += Widths[Index] + 6.0f * Scale;
	}
	const float GroupGap = 18.0f * Scale;
	Width += GroupGap;

	float X = static_cast<float>(Size.X) - Margin - Width;
	const float Y = static_cast<float>(Size.Y) - Margin - 40.0f * Scale;
	PaintText(Out, Layer, Geometry, TEXT("MOVE"), LabelFont, FVector2D(X, Y - 18.0f * Scale), WithAlpha(AmplitudeStyle::TextDim, Alpha));
	for (int32 Index = 0; Index < 5; ++Index)
	{
		if (Index == 2)
		{
			X += GroupGap;
			PaintText(Out, Layer, Geometry, TEXT("FIRE"), LabelFont, FVector2D(X, Y - 18.0f * Scale), WithAlpha(AmplitudeStyle::TextDim, Alpha));
		}
		const FLinearColor Tint = Index < 2 ? AmplitudeStyle::TextDim : AmplitudeStyle::GetColumnColor(Index - 2);
		X += PaintKeycap(Out, Layer, Geometry, FVector2D(X, Y), Labels[Index], Tint, Scale, Alpha) + 6.0f * Scale;
	}
	return Layer + 3;
}

int32 SAmplitudeGameView::PaintPopups(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, float Scale) const
{
	const FVector2D Size(Geometry.GetLocalSize());
	for (const FPopup& Popup : Popups)
	{
		FVector2D Anchor;
		if (!ProjectToLocal(Popup.WorldLocation, Size, Anchor))
		{
			continue;
		}
		const float T = Popup.Age / Popup.Life;
		const float Pop = T < 0.15f ? FMath::Lerp(1.35f, 1.0f, T / 0.15f) : 1.0f;
		const FVector2D Position = Anchor + (Popup.Offset + FVector2D(0.0, -40.0 * T)) * Scale;
		const float Alpha = FMath::Min(1.0f, (1.0f - T) * 2.5f);
		PaintText(Out, Layer, Geometry, Popup.Text, AmplitudeStyle::Font(Popup.Size * Scale * Pop), Position, WithAlpha(Popup.Color, Alpha), FVector2D(0.5, 0.5));
	}
	return Layer + 2;
}

int32 SAmplitudeGameView::PaintCenterMessages(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FAmplitudeSession& Session, float Scale) const
{
	const Amp::FSimulation& Simulation = Session.GetSimulation();
	const FVector2D Size(Geometry.GetLocalSize());
	const FVector2D Center(Size.X * 0.5, Size.Y * 0.36);
	const FSlateFontInfo HugeFont = AmplitudeStyle::Font(110.0f * Scale);
	const FSlateFontInfo BigFont = AmplitudeStyle::Font(52.0f * Scale);
	const FSlateFontInfo MediumFont = AmplitudeStyle::Font(24.0f * Scale);

	const AAmplitudeDirector* Owner = Director.Get();
	const double ResumeCountdown = Owner != nullptr ? Owner->GetResumeCountdownSeconds() : 0.0;
	const double SongTimeMs = Session.GetSongTimeMs();

	if (ResumeCountdown > 0.0)
	{
		const int32 Step = FMath::CeilToInt32(ResumeCountdown / 0.4);
		DrawBox(Out, Layer, Geometry, FVector2D::ZeroVector, Size, FLinearColor(0.0f, 0.0f, 0.05f, 0.3f));
		PaintText(Out, Layer + 1, Geometry, FString::FromInt(Step), HugeFont, Center, AmplitudeStyle::Accent, FVector2D(0.5, 0.5));
	}
	else if (SongTimeMs < 0.0)
	{
		// Lead-in: song title, then a 3-2-1 count into the first beat.
		const FAmplitudeSongDefinition& Song = Session.GetSong();
		PaintText(Out, Layer + 1, Geometry, Song.Title.ToUpper(), BigFont, Center - FVector2D(0.0, 80.0 * Scale), AmplitudeStyle::Text, FVector2D(0.5, 0.5));
		const FString Subtitle = FString::Printf(TEXT("%s%s"), Song.Artist.IsEmpty() ? TEXT("") : *(Song.Artist.ToUpper() + TEXT("   ")),
			*FString(ANSI_TO_TCHAR(Amp::GetDifficultyName(Session.GetDifficulty()))).ToUpper());
		PaintText(Out, Layer + 1, Geometry, Subtitle, MediumFont, Center - FVector2D(0.0, 30.0 * Scale), AmplitudeStyle::GetDifficultyColor(Session.GetDifficulty()), FVector2D(0.5, 0.5));
		const int32 Count = FMath::CeilToInt32(-SongTimeMs / 1000.0);
		const FString CountText = Count <= 3 ? FString::FromInt(Count) : FString(TEXT("GET READY"));
		PaintText(Out, Layer + 1, Geometry, CountText, Count <= 3 ? HugeFont : BigFont, Center + FVector2D(0.0, 60.0 * Scale), AmplitudeStyle::Accent, FVector2D(0.5, 0.5));
	}
	else if (SongTimeMs < 600.0 && !Simulation.IsFinished())
	{
		PaintText(Out, Layer + 1, Geometry, TEXT("GO!"), HugeFont, Center, WithAlpha(AmplitudeStyle::Accent, 1.0f - static_cast<float>(SongTimeMs / 600.0)), FVector2D(0.5, 0.5));
	}

	for (const FBanner& Banner : Banners)
	{
		const float T = Banner.Age / Banner.Life;
		const float Alpha = FMath::Min(1.0f, FMath::Min(Banner.Age * 8.0f, (1.0f - T) * 4.0f));
		const float Pop = Banner.Age < 0.12f ? FMath::Lerp(1.3f, 1.0f, Banner.Age / 0.12f) : 1.0f;
		PaintText(Out, Layer + 2, Geometry, Banner.Text, AmplitudeStyle::Font(46.0f * Scale * Pop), FVector2D(Center.X, Size.Y * 0.24), WithAlpha(Banner.Color, Alpha), FVector2D(0.5, 0.5));
	}

	if (Simulation.IsFinished())
	{
		DrawBox(Out, Layer, Geometry, FVector2D::ZeroVector, Size, FLinearColor(0.0f, 0.0f, 0.05f, 0.25f));
	}
	return Layer + 4;
}
