#pragma once

#include "CoreMinimal.h"
#include "Core/AmpTypes.h"
#include "GameFramework/GameUserSettings.h"
#include "InputCoreTypes.h"

#include "AmplitudeUserSettings.generated.h"

UENUM()
enum class EAmplitudeColorblindMode : uint8
{
	Off,
	Deuteranopia,
	Protanopia,
	Tritanopia
};

UENUM()
enum class EAmplitudeControllerMode : uint8
{
	AutoDetect,
	Keyboard,
	Gamepad
};

namespace AmplitudeControls
{
	/** Binding slots per lane: primary key, number-row alternate, second alternate, gamepad button. */
	constexpr int32 NumSlots = 4;
	constexpr int32 GamepadSlot = 3;
}

USTRUCT()
struct FAmplitudeLaneBinding
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FKey> Keys;
};

/** A named set of key bindings (spec 8.4). Each lane (and pause) has AmplitudeControls::NumSlots slots. */
USTRUCT()
struct FAmplitudeControlProfile
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	TArray<FAmplitudeLaneBinding> Lanes;

	/** Same slot layout as a lane. */
	UPROPERTY()
	TArray<FKey> PauseKeys;

	static FAmplitudeControlProfile MakeDefault();

	/** Guarantees NumLanes lanes with NumSlots slots each (config files may be hand-edited). */
	void Sanitize();
	FKey GetLaneKey(int32 Lane, int32 Slot) const;
	void SetLaneKey(int32 Lane, int32 Slot, const FKey& Key);
	FKey GetPauseKey(int32 Slot) const;
	void SetPauseKey(int32 Slot, const FKey& Key);
	/** Every valid key bound to the lane (all slots). */
	TArray<FKey> GetLaneKeys(int32 Lane) const;
	TArray<FKey> GetPauseKeys() const;
	/** The key to show on screen for a lane: the gamepad slot or the primary keyboard key. */
	FKey GetDisplayKey(int32 Lane, bool bGamepad) const;
};

/** Every persistent option from the settings menu (spec 11.4), saved to GameUserSettings.ini. */
UCLASS(config = GameUserSettings, configdonotcheckdefaults)
class AMPLITUDE_API UAmplitudeUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	UAmplitudeUserSettings(const FObjectInitializer& ObjectInitializer);

	static UAmplitudeUserSettings* Get();

	virtual void SetToDefaults() override;
	virtual void LoadSettings(bool bForceReload = false) override;

	void ResetAmplitudeDefaults();

	/** Linear gains after the perceptual volume curve; music sits 3 dB under effects (spec 10.4.3). */
	float GetMusicGain() const;
	float GetSfxGain() const;

	Amp::EDifficulty GetLastDifficulty() const;
	void SetLastDifficulty(Amp::EDifficulty Difficulty);

	const FAmplitudeControlProfile& GetActiveProfile() const;
	FAmplitudeControlProfile& GetMutableActiveProfile();
	int32 AddProfileFromActive();
	void RemoveActiveProfile();

	/** Lane colours for the current colourblind mode (or the custom palette when one is configured). */
	FLinearColor GetLaneColor(int32 Lane) const;

	// Audio
	UPROPERTY(config)
	float MasterVolume = 0.8f;

	UPROPERTY(config)
	float MusicVolume = 0.9f;

	UPROPERTY(config)
	float SfxVolume = 0.8f;

	/** Positive values compensate for audio that is heard late (spec 10.2.2, +/-500 ms). */
	UPROPERTY(config)
	int32 AudioOffsetMs = 0;

	// Gameplay
	UPROPERTY(config)
	bool bShowCombo = true;

	UPROPERTY(config)
	bool bShowEnergy = true;

	/** FPS, audio sync drift and input latency (spec 11.2.4). */
	UPROPERTY(config)
	bool bShowPerformanceStats = false;

	UPROPERTY(config)
	EAmplitudeColorblindMode ColorblindMode = EAmplitudeColorblindMode::Off;

	/** Optional custom lane colours (six entries) - the "colour configuration file" from the FAQ. */
	UPROPERTY(config)
	TArray<FLinearColor> CustomLaneColors;

	UPROPERTY(config)
	FString PlayerName;

	UPROPERTY(config)
	uint8 LastDifficulty = 1;

	UPROPERTY(config)
	FString LastSongId;

	UPROPERTY(config)
	TArray<FString> AdditionalSongDirectories;

	// Controls
	UPROPERTY(config)
	EAmplitudeControllerMode ControllerMode = EAmplitudeControllerMode::AutoDetect;

	UPROPERTY(config)
	TArray<FAmplitudeControlProfile> ControlProfiles;

	UPROPERTY(config)
	int32 ActiveProfileIndex = 0;

private:
	void SanitizeAmplitudeSettings();
};
