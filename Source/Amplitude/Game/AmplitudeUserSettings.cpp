#include "Game/AmplitudeUserSettings.h"

#include "Engine/Engine.h"
#include "HAL/PlatformProcess.h"

namespace
{
	FLinearColor Hex(const TCHAR* HexCode)
	{
		return FLinearColor(FColor::FromHex(HexCode));
	}

	/** Spec 12.2.1 default palette. */
	const TCHAR* const DefaultPalette[Amp::NumLanes] = {TEXT("FF1744"), TEXT("D500F9"), TEXT("00B0FF"), TEXT("00E676"), TEXT("FF6E40"), TEXT("00E5FF")};
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

FAmplitudeControlProfile FAmplitudeControlProfile::MakeDefault()
{
	// Spec 8.1: A-H / 1-6 / arrows+space+R+T, and a gamepad layout of D-pad (lanes 1-3) + face buttons (lanes 4-6).
	const FKey Primary[Amp::NumLanes] = {EKeys::A, EKeys::S, EKeys::D, EKeys::F, EKeys::G, EKeys::H};
	const FKey Numbers[Amp::NumLanes] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six};
	const FKey Alternate[Amp::NumLanes] = {EKeys::Left, EKeys::Up, EKeys::Right, EKeys::SpaceBar, EKeys::R, EKeys::T};
	const FKey Gamepad[Amp::NumLanes] = {EKeys::Gamepad_DPad_Left, EKeys::Gamepad_DPad_Up, EKeys::Gamepad_DPad_Right,
		EKeys::Gamepad_FaceButton_Left, EKeys::Gamepad_FaceButton_Top, EKeys::Gamepad_FaceButton_Right};

	FAmplitudeControlProfile Profile;
	Profile.Name = TEXT("Default");
	for (int32 Lane = 0; Lane < Amp::NumLanes; ++Lane)
	{
		FAmplitudeLaneBinding Binding;
		Binding.Keys = {Primary[Lane], Numbers[Lane], Alternate[Lane], Gamepad[Lane]};
		Profile.Lanes.Add(Binding);
	}
	Profile.PauseKeys = {EKeys::P, EKeys::Escape, EKeys::Tab, EKeys::Gamepad_Special_Right};
	return Profile;
}

void FAmplitudeControlProfile::Sanitize()
{
	if (Name.IsEmpty())
	{
		Name = TEXT("Profile");
	}
	Lanes.SetNum(Amp::NumLanes);
	for (FAmplitudeLaneBinding& Binding : Lanes)
	{
		if (Binding.Keys.Num() < AmplitudeControls::NumSlots)
		{
			Binding.Keys.SetNum(AmplitudeControls::NumSlots);
		}
	}
	if (PauseKeys.Num() < AmplitudeControls::NumSlots)
	{
		PauseKeys.SetNum(AmplitudeControls::NumSlots);
	}
}

FKey FAmplitudeControlProfile::GetLaneKey(int32 Lane, int32 Slot) const
{
	if (!Lanes.IsValidIndex(Lane) || !Lanes[Lane].Keys.IsValidIndex(Slot))
	{
		return EKeys::Invalid;
	}
	return Lanes[Lane].Keys[Slot];
}

void FAmplitudeControlProfile::SetLaneKey(int32 Lane, int32 Slot, const FKey& Key)
{
	if (!Lanes.IsValidIndex(Lane) || Slot < 0)
	{
		return;
	}
	TArray<FKey>& Keys = Lanes[Lane].Keys;
	if (Keys.Num() <= Slot)
	{
		Keys.SetNum(Slot + 1);
	}
	Keys[Slot] = Key;
}

FKey FAmplitudeControlProfile::GetPauseKey(int32 Slot) const
{
	return PauseKeys.IsValidIndex(Slot) ? PauseKeys[Slot] : EKeys::Invalid;
}

void FAmplitudeControlProfile::SetPauseKey(int32 Slot, const FKey& Key)
{
	if (Slot < 0)
	{
		return;
	}
	if (PauseKeys.Num() <= Slot)
	{
		PauseKeys.SetNum(Slot + 1);
	}
	PauseKeys[Slot] = Key;
}

TArray<FKey> FAmplitudeControlProfile::GetLaneKeys(int32 Lane) const
{
	TArray<FKey> Result;
	if (Lanes.IsValidIndex(Lane))
	{
		for (const FKey& Key : Lanes[Lane].Keys)
		{
			if (Key.IsValid())
			{
				Result.AddUnique(Key);
			}
		}
	}
	return Result;
}

TArray<FKey> FAmplitudeControlProfile::GetPauseKeys() const
{
	TArray<FKey> Result;
	for (const FKey& Key : PauseKeys)
	{
		if (Key.IsValid())
		{
			Result.AddUnique(Key);
		}
	}
	return Result;
}

FKey FAmplitudeControlProfile::GetDisplayKey(int32 Lane, bool bGamepad) const
{
	if (Lanes.IsValidIndex(Lane))
	{
		for (const FKey& Key : Lanes[Lane].Keys)
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
