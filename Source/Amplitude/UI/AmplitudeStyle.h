#pragma once

#include "CoreMinimal.h"
#include "Core/AmpSfxSynth.h"
#include "Core/AmpTypes.h"
#include "Fonts/SlateFontInfo.h"
#include "InputCoreTypes.h"
#include "Styling/SlateTypes.h"

struct FSlateBrush;

/** Colours, fonts and brushes for the neon look of spec section 12. Everything is code-defined (no assets). */
namespace AmplitudeStyle
{
	FLinearColor Hex(const TCHAR* HexCode);

	extern const FLinearColor Background;
	extern const FLinearColor Panel;
	extern const FLinearColor PanelOutline;
	extern const FLinearColor Text;
	extern const FLinearColor TextDim;
	extern const FLinearColor Accent;
	extern const FLinearColor Perfect;
	extern const FLinearColor Good;
	extern const FLinearColor Miss;
	extern const FLinearColor EnergyHigh;
	extern const FLinearColor EnergyMid;
	extern const FLinearColor EnergyLow;
	extern const FLinearColor Shield;
	extern const FLinearColor Fever;

	/** Lane colour honouring the colourblind/custom palette in the user settings. */
	FLinearColor GetLaneColor(int32 Lane);
	FLinearColor GetPowerupColor(Amp::EPowerupType Type);
	/** Short label drawn inside a powerup gem. */
	const TCHAR* GetPowerupGlyph(Amp::EPowerupType Type);
	FLinearColor GetDifficultyColor(Amp::EDifficulty Difficulty);
	/** Green -> yellow -> red gradient for the energy bar (spec 3.1.1). */
	FLinearColor GetEnergyColor(float Fraction);

	FSlateFontInfo Font(float Size, bool bBold = true);

	const FSlateBrush* WhiteBrush();
	const FSlateBrush* RoundedBrush();
	const FSlateBrush* PanelBrush();
	const FButtonStyle& ButtonStyle();
	/** Borderless style for the small < > arrows of option rows. */
	const FButtonStyle& ArrowButtonStyle();

	FText FormatScore(int64 Score);
	FText FormatTime(double Milliseconds);
	FText GetKeyLabel(const FKey& Key);
	FText GetLaneLabel(int32 Lane);
}

namespace AmplitudeUI
{
	/** Menu widgets play navigation sounds through this hook (the director installs it). */
	void SetSoundHandler(TFunction<void(Amp::ESfx)> Handler);
	void PlayUiSfx(Amp::ESfx Sfx);
}
