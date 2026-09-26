#include "UI/AmplitudeStyle.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Game/AmplitudeUserSettings.h"
#include "Styling/CoreStyle.h"

namespace AmplitudeStyle
{
	FLinearColor Hex(const TCHAR* HexCode)
	{
		return FLinearColor(FColor::FromHex(HexCode));
	}

	const FLinearColor Background(FColor(12, 14, 30));
	const FLinearColor Panel(FColor(16, 20, 44, 215));
	const FLinearColor PanelOutline(FColor(255, 255, 255, 60));
	const FLinearColor Text(FColor(246, 247, 252));
	const FLinearColor TextDim(FColor(168, 174, 200));
	const FLinearColor Accent(FColor(255, 138, 101));
	const FLinearColor Perfect(FColor(255, 205, 60));
	const FLinearColor Good(FColor(190, 225, 255));
	const FLinearColor Miss(FColor(255, 82, 82));
	const FLinearColor EnergyHigh(FColor(46, 211, 160));
	const FLinearColor EnergyMid(FColor(255, 198, 61));
	const FLinearColor EnergyLow(FColor(255, 90, 95));
	const FLinearColor Shield(FColor(142, 197, 255));
	const FLinearColor Fever(FColor(255, 140, 66));

	FLinearColor GetLaneColor(int32 Lane)
	{
		if (const UAmplitudeUserSettings* Settings = UAmplitudeUserSettings::Get())
		{
			return Settings->GetLaneColor(Lane);
		}
		const TCHAR* const Palette[Amp::NumLanes] = {TEXT("FF5A5F"), TEXT("9B6BFF"), TEXT("3D8BFF"), TEXT("FFC63D"), TEXT("2ED3A0"), TEXT("FF8C42")};
		return Hex(Palette[FMath::Clamp(Lane, 0, Amp::NumLanes - 1)]);
	}

	FLinearColor GetColumnColor(int32 Column)
	{
		const TCHAR* const Palette[Amp::NumColumns] = {TEXT("8EC5FF"), TEXT("FFFFFF"), TEXT("FFB4A2")};
		return Hex(Palette[FMath::Clamp(Column, 0, Amp::NumColumns - 1)]);
	}

	FLinearColor GetPowerupColor(Amp::EPowerupType Type)
	{
		switch (Type)
		{
		case Amp::EPowerupType::Score2x:
			return FLinearColor(FColor(255, 198, 61));
		case Amp::EPowerupType::LaneCleaner:
			return FLinearColor(FColor(79, 195, 247));
		case Amp::EPowerupType::SlowMotion:
			return FLinearColor(FColor(155, 107, 255));
		case Amp::EPowerupType::Shield:
			return FLinearColor(FColor(227, 236, 255));
		case Amp::EPowerupType::Fever:
			return FLinearColor(FColor(255, 140, 66));
		case Amp::EPowerupType::AutoCapture:
			return FLinearColor(FColor(46, 211, 160));
		}
		return FLinearColor::White;
	}

	const TCHAR* GetPowerupGlyph(Amp::EPowerupType Type)
	{
		switch (Type)
		{
		case Amp::EPowerupType::Score2x:
			return TEXT("2X");
		case Amp::EPowerupType::LaneCleaner:
			return TEXT("LC");
		case Amp::EPowerupType::SlowMotion:
			return TEXT("SL");
		case Amp::EPowerupType::Shield:
			return TEXT("SH");
		case Amp::EPowerupType::Fever:
			return TEXT("FV");
		case Amp::EPowerupType::AutoCapture:
			return TEXT("AC");
		}
		return TEXT("?");
	}

	FLinearColor GetDifficultyColor(Amp::EDifficulty Difficulty)
	{
		switch (Difficulty)
		{
		case Amp::EDifficulty::Mellow:
			return FLinearColor(FColor(46, 211, 160));
		case Amp::EDifficulty::Normal:
			return FLinearColor(FColor(61, 139, 255));
		case Amp::EDifficulty::Brutal:
			return FLinearColor(FColor(255, 140, 66));
		case Amp::EDifficulty::Insane:
			return FLinearColor(FColor(255, 90, 95));
		}
		return Accent;
	}

	FLinearColor GetEnergyColor(float Fraction)
	{
		const float Clamped = FMath::Clamp(Fraction, 0.0f, 1.0f);
		if (Clamped < 0.35f)
		{
			return FMath::Lerp(EnergyLow, EnergyMid, Clamped / 0.35f);
		}
		return FMath::Lerp(EnergyMid, EnergyHigh, FMath::Min(1.0f, (Clamped - 0.35f) / 0.35f));
	}

	FSlateFontInfo Font(float Size, bool bBold)
	{
		const int32 Points = FMath::Max(6, FMath::RoundToInt32(Size));
		return FCoreStyle::GetDefaultFontStyle(bBold ? FName(TEXT("Bold")) : FName(TEXT("Regular")), Points);
	}

	const FSlateBrush* WhiteBrush()
	{
		static const FSlateColorBrush Brush(FLinearColor::White);
		return &Brush;
	}

	const FSlateBrush* RoundedBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 6.0f);
		return &Brush;
	}

	const FSlateBrush* PanelBrush()
	{
		static const FSlateRoundedBoxBrush Brush(Panel, 14.0f, PanelOutline, 1.5f);
		return &Brush;
	}

	const FButtonStyle& ButtonStyle()
	{
		static const FButtonStyle Style = []()
		{
			// White brushes: the button tints them (normal / focused / selected) through ButtonColorAndOpacity.
			const FSlateRoundedBoxBrush Normal(FLinearColor(1.0f, 1.0f, 1.0f, 0.07f), 8.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.4f), 1.5f);
			const FSlateRoundedBoxBrush Pressed(FLinearColor(1.0f, 1.0f, 1.0f, 0.22f), 8.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.9f), 2.0f);
			const FSlateRoundedBoxBrush Disabled(FLinearColor(1.0f, 1.0f, 1.0f, 0.02f), 8.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.12f), 1.0f);
			return FButtonStyle()
				.SetNormal(Normal)
				.SetHovered(Normal)
				.SetPressed(Pressed)
				.SetDisabled(Disabled)
				.SetNormalPadding(FMargin(18.0f, 10.0f))
				.SetPressedPadding(FMargin(18.0f, 11.0f, 18.0f, 9.0f));
		}();
		return Style;
	}

	const FButtonStyle& ArrowButtonStyle()
	{
		static const FButtonStyle Style = []()
		{
			const FSlateNoResource None;
			const FSlateRoundedBoxBrush Pressed(FLinearColor(1.0f, 1.0f, 1.0f, 0.15f), 6.0f);
			return FButtonStyle()
				.SetNormal(None)
				.SetHovered(Pressed)
				.SetPressed(Pressed)
				.SetDisabled(None)
				.SetNormalPadding(FMargin(0.0f))
				.SetPressedPadding(FMargin(0.0f));
		}();
		return Style;
	}

	FText FormatScore(int64 Score)
	{
		return FText::AsNumber(Score);
	}

	FText FormatTime(double Milliseconds)
	{
		const int32 TotalSeconds = FMath::Max(0, FMath::FloorToInt32(Milliseconds / 1000.0));
		return FText::FromString(FString::Printf(TEXT("%d:%02d"), TotalSeconds / 60, TotalSeconds % 60));
	}

	FText GetKeyLabel(const FKey& Key)
	{
		if (!Key.IsValid())
		{
			return FText::FromString(TEXT("-"));
		}

		struct FShortName
		{
			FKey Key;
			const TCHAR* Label;
		};
		static const FShortName ShortNames[] = {
			{EKeys::Gamepad_DPad_Left, TEXT("D-LEFT")},
			{EKeys::Gamepad_DPad_Right, TEXT("D-RIGHT")},
			{EKeys::Gamepad_DPad_Up, TEXT("D-UP")},
			{EKeys::Gamepad_DPad_Down, TEXT("D-DOWN")},
			{EKeys::Gamepad_FaceButton_Bottom, TEXT("A / CROSS")},
			{EKeys::Gamepad_FaceButton_Right, TEXT("B / CIRCLE")},
			{EKeys::Gamepad_FaceButton_Left, TEXT("X / SQUARE")},
			{EKeys::Gamepad_FaceButton_Top, TEXT("Y / TRIANGLE")},
			{EKeys::Gamepad_LeftShoulder, TEXT("LB")},
			{EKeys::Gamepad_RightShoulder, TEXT("RB")},
			{EKeys::Gamepad_LeftTrigger, TEXT("LT")},
			{EKeys::Gamepad_RightTrigger, TEXT("RT")},
			{EKeys::Gamepad_Special_Right, TEXT("START")},
			{EKeys::Gamepad_LeftStick_Left, TEXT("L-STICK LEFT")},
			{EKeys::Gamepad_LeftStick_Right, TEXT("L-STICK RIGHT")},
			{EKeys::Left, TEXT("LEFT")},
			{EKeys::Right, TEXT("RIGHT")},
			{EKeys::Gamepad_Special_Left, TEXT("BACK")},
			{EKeys::SpaceBar, TEXT("SPACE")},
			{EKeys::Escape, TEXT("ESC")},
		};
		for (const FShortName& Entry : ShortNames)
		{
			if (Entry.Key == Key)
			{
				return FText::FromString(Entry.Label);
			}
		}
		return FText::FromString(Key.GetDisplayName(false).ToString().ToUpper());
	}

	FText GetLaneLabel(int32 Lane)
	{
		return FText::FromString(FString(ANSI_TO_TCHAR(Amp::GetLaneInstrumentName(Lane))).ToUpper());
	}
}

namespace AmplitudeUI
{
	namespace
	{
		TFunction<void(Amp::ESfx)>& GetSoundHandler()
		{
			static TFunction<void(Amp::ESfx)> Handler;
			return Handler;
		}
	}

	void SetSoundHandler(TFunction<void(Amp::ESfx)> Handler)
	{
		GetSoundHandler() = MoveTemp(Handler);
	}

	void PlayUiSfx(Amp::ESfx Sfx)
	{
		if (const TFunction<void(Amp::ESfx)>& Handler = GetSoundHandler())
		{
			Handler(Sfx);
		}
	}
}
