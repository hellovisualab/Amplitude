#include "Game/AmplitudeUserSettings.h"

#include "Engine/Engine.h"
#include "HAL/PlatformProcess.h"

namespace
{
	FLinearColor Hex(const TCHAR* HexCode)
	{
		return FLinearColor(FColor::FromHex(HexCode));
	}

	/** Default palette: bright, friendly pop colours (coral, violet, azure, sunflower, mint, tangerine). */
	const TCHAR* const DefaultPalette[Amp::NumLanes] = {TEXT("FF5A5F"), TEXT("9B6BFF"), TEXT("3D8BFF"), TEXT("FFC63D"), TEXT("2ED3A0"), TEXT("FF8C42")};
	/** Okabe-Ito based palette, distinguishable with red-green colour vision deficiencies. */
	const TCHAR* const RedGreenSafePalette[Amp::NumLanes] = {TEXT("D55E00"), TEXT("CC79A7"), TEXT("0072B2"), TEXT("F0E442"), TEXT("E69F00"), TEXT("56B4E9")};
	/** Avoids blue/yellow and blue/green pairs for tritanopia. */
	const TCHAR* const TritanSafePalette[Amp::NumLanes] = {TEXT("FF1744"), TEXT("FF80AB"), TEXT("00BFA5"), TEXT("F5F5F5"), TEXT("B388FF"), TEXT("FF9100")};

	/** Maps the 0-1 slider value onto a 40 dB range, which sounds evenly spaced. */
	float PerceptualGain(float Volume)
	{
		const float Clamped = FMath::Clamp(Volume, 0.0f, 1.0f);
		return Clamped * Clamped;
	}
}

FText AmplitudeControls::GetActionName(int32 Action)
{
	switch (Action)
	{
	case MoveLeft:
		return NSLOCTEXT("Amplitude", "ActionMoveLeft", "Move Left");
	case MoveRight:
		return NSLOCTEXT("Amplitude", "ActionMoveRight", "Move Right");
	case GemLeft:
		return NSLOCTEXT("Amplitude", "ActionGemLeft", "Left Gem");
	case GemMiddle:
		return NSLOCTEXT("Amplitude", "ActionGemMiddle", "Middle Gem");
	case GemRight:
		return NSLOCTEXT("Amplitude", "ActionGemRight", "Right Gem");
	case Pause:
		return NSLOCTEXT("Amplitude", "ActionPause", "Pause");
	default:
		return FText::GetEmpty();
	}
}

FAmplitudeControlProfile FAmplitudeControlProfile::MakeDefault()
{
	using namespace AmplitudeControls;

	// Left hand steers (A/D), right hand fires (J/K/L); arrows + Z/X/C for the other way round.
	// Gamepad follows the original game: D-pad steers, L1 / R1 / R2 fire.
	const FKey Primary[NumActions] = {EKeys::A, EKeys::D, EKeys::J, EKeys::K, EKeys::L, EKeys::Escape};
	const FKey Alternate[NumActions] = {EKeys::Left, EKeys::Right, EKeys::Z, EKeys::X, EKeys::C, EKeys::P};
	const FKey Gamepad[NumActions] = {EKeys::Gamepad_DPad_Left, EKeys::Gamepad_DPad_Right, EKeys::Gamepad_LeftShoulder,
		EKeys::Gamepad_RightShoulder, EKeys::Gamepad_RightTrigger, EKeys::Gamepad_Special_Right};

	FAmplitudeControlProfile Profile;
	Profile.Name = TEXT("Default");
	Profile.Version = LayoutVersion;
	for (int32 Action = 0; Action < NumActions; ++Action)
	{
		FAmplitudeActionBinding Binding;
		Binding.Keys = {Primary[Action], Alternate[Action], Gamepad[Action]};
		Profile.Actions.Add(Binding);
	}
	return Profile;
}

void FAmplitudeControlProfile::Sanitize()
{
	if (Version != AmplitudeControls::LayoutVersion)
	{
		// Saved by an older build with a different set of actions: start again from the defaults.
		const FString OldName = Name;
		*this = MakeDefault();
		Name = OldName.IsEmpty() ? Name : OldName;
	}
	if (Name.IsEmpty())
	{
		Name = TEXT("Profile");
	}
	Actions.SetNum(AmplitudeControls::NumActions);
	for (FAmplitudeActionBinding& Binding : Actions)
	{
		Binding.Keys.SetNum(AmplitudeControls::NumSlots);
	}
}

FKey FAmplitudeControlProfile::GetKey(int32 Action, int32 Slot) const
{
	if (!Actions.IsValidIndex(Action) || !Actions[Action].Keys.IsValidIndex(Slot))
	{
		return EKeys::Invalid;
	}
	return Actions[Action].Keys[Slot];
}

void FAmplitudeControlProfile::SetKey(int32 Action, int32 Slot, const FKey& Key)
{
	if (!Actions.IsValidIndex(Action) || Slot < 0)
	{
		return;
	}
	TArray<FKey>& Keys = Actions[Action].Keys;
	if (Keys.Num() <= Slot)
	{
		Keys.SetNum(Slot + 1);
	}
	Keys[Slot] = Key;
}

TArray<FKey> FAmplitudeControlProfile::GetKeys(int32 Action) const
{
	TArray<FKey> Result;
	if (Actions.IsValidIndex(Action))
	{
		for (const FKey& Key : Actions[Action].Keys)
		{
			if (Key.IsValid())
			{
				Result.AddUnique(Key);
			}
		}
	}
	return Result;
}

FKey FAmplitudeControlProfile::GetDisplayKey(int32 Action, bool bGamepad) const
{
	if (Actions.IsValidIndex(Action))
	{
		for (const FKey& Key : Actions[Action].Keys)
		{
			if (Key.IsValid() && Key.IsGamepadKey() == bGamepad)
			{
				return Key;
			}
		}
	}
	return EKeys::Invalid;
}

UAmplitudeUserSettings::UAmplitudeUserSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ResetAmplitudeDefaults();
}

UAmplitudeUserSettings* UAmplitudeUserSettings::Get()
{
	return GEngine != nullptr ? Cast<UAmplitudeUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

void UAmplitudeUserSettings::SetToDefaults()
{
	Super::SetToDefaults();
	ResetAmplitudeDefaults();
}

void UAmplitudeUserSettings::LoadSettings(bool bForceReload)
{
	Super::LoadSettings(bForceReload);
	SanitizeAmplitudeSettings();
}

void UAmplitudeUserSettings::ResetAmplitudeDefaults()
{
	MasterVolume = 0.8f;
	MusicVolume = 0.9f;
	SfxVolume = 0.8f;
	AudioOffsetMs = 0;
	bShowCombo = true;
	bShowEnergy = true;
	bShowPerformanceStats = false;
	ColorblindMode = EAmplitudeColorblindMode::Off;
	CustomLaneColors.Reset();
	ControllerMode = EAmplitudeControllerMode::AutoDetect;
	ControlProfiles = {FAmplitudeControlProfile::MakeDefault()};
	ActiveProfileIndex = 0;
	if (PlayerName.IsEmpty())
	{
		PlayerName = FPlatformProcess::UserName();
	}
	if (PlayerName.IsEmpty())
	{
		PlayerName = TEXT("Player");
	}
}

void UAmplitudeUserSettings::SanitizeAmplitudeSettings()
{
	MasterVolume = FMath::Clamp(MasterVolume, 0.0f, 1.0f);
	MusicVolume = FMath::Clamp(MusicVolume, 0.0f, 1.0f);
	SfxVolume = FMath::Clamp(SfxVolume, 0.0f, 1.0f);
	AudioOffsetMs = FMath::Clamp(AudioOffsetMs, -500, 500);
	LastDifficulty = static_cast<uint8>(FMath::Clamp<int32>(LastDifficulty, 0, Amp::NumDifficulties - 1));
	if (ControlProfiles.Num() == 0)
	{
		ControlProfiles.Add(FAmplitudeControlProfile::MakeDefault());
	}
	for (FAmplitudeControlProfile& Profile : ControlProfiles)
	{
		Profile.Sanitize();
	}
	ActiveProfileIndex = FMath::Clamp(ActiveProfileIndex, 0, ControlProfiles.Num() - 1);
	if (PlayerName.TrimStartAndEnd().IsEmpty())
	{
		PlayerName = TEXT("Player");
	}
}

float UAmplitudeUserSettings::GetMusicGain() const
{
	constexpr float MinusThreeDb = 0.7079f;
	return PerceptualGain(MasterVolume) * PerceptualGain(MusicVolume) * MinusThreeDb;
}

float UAmplitudeUserSettings::GetSfxGain() const
{
	return PerceptualGain(MasterVolume) * PerceptualGain(SfxVolume);
}

Amp::EDifficulty UAmplitudeUserSettings::GetLastDifficulty() const
{
	return static_cast<Amp::EDifficulty>(FMath::Clamp<int32>(LastDifficulty, 0, Amp::NumDifficulties - 1));
}

void UAmplitudeUserSettings::SetLastDifficulty(Amp::EDifficulty Difficulty)
{
	LastDifficulty = static_cast<uint8>(Difficulty);
}

const FAmplitudeControlProfile& UAmplitudeUserSettings::GetActiveProfile() const
{
	return const_cast<UAmplitudeUserSettings*>(this)->GetMutableActiveProfile();
}

FAmplitudeControlProfile& UAmplitudeUserSettings::GetMutableActiveProfile()
{
	if (ControlProfiles.Num() == 0)
	{
		ControlProfiles.Add(FAmplitudeControlProfile::MakeDefault());
	}
	ActiveProfileIndex = FMath::Clamp(ActiveProfileIndex, 0, ControlProfiles.Num() - 1);
	FAmplitudeControlProfile& Profile = ControlProfiles[ActiveProfileIndex];
	Profile.Sanitize();
	return Profile;
}

int32 UAmplitudeUserSettings::AddProfileFromActive()
{
	FAmplitudeControlProfile Copy = GetActiveProfile();
	Copy.Name = FString::Printf(TEXT("Profile %d"), ControlProfiles.Num() + 1);
	ActiveProfileIndex = ControlProfiles.Add(Copy);
	return ActiveProfileIndex;
}

void UAmplitudeUserSettings::RemoveActiveProfile()
{
	if (ControlProfiles.Num() <= 1)
	{
		ControlProfiles = {FAmplitudeControlProfile::MakeDefault()};
		ActiveProfileIndex = 0;
		return;
	}
	ControlProfiles.RemoveAt(ActiveProfileIndex);
	ActiveProfileIndex = FMath::Clamp(ActiveProfileIndex, 0, ControlProfiles.Num() - 1);
}

FLinearColor UAmplitudeUserSettings::GetLaneColor(int32 Lane) const
{
	const int32 Index = FMath::Clamp(Lane, 0, Amp::NumLanes - 1);
	if (CustomLaneColors.Num() >= Amp::NumLanes && ColorblindMode == EAmplitudeColorblindMode::Off)
	{
		return CustomLaneColors[Index];
	}
	switch (ColorblindMode)
	{
	case EAmplitudeColorblindMode::Deuteranopia:
	case EAmplitudeColorblindMode::Protanopia:
		return Hex(RedGreenSafePalette[Index]);
	case EAmplitudeColorblindMode::Tritanopia:
		return Hex(TritanSafePalette[Index]);
	default:
		return Hex(DefaultPalette[Index]);
	}
}
