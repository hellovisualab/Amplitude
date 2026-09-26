#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "UI/SAmplitudeWidgets.h"

class UAmplitudeUserSettings;
struct FAmplitudeControlProfile;

/** Spec 11.4: audio, graphics, gameplay and control settings, applied live and saved on Back. */
class SAmplitudeSettingsScreen : public SAmplitudeScreen
{
public:
	SLATE_BEGIN_ARGS(SAmplitudeSettingsScreen) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AAmplitudeDirector>, Director)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

protected:
	virtual void OnBack() override;

private:
	static UAmplitudeUserSettings* GetSettings();

	TSharedRef<SWidget> MakeVolumeRow(const FText& Label, float UAmplitudeUserSettings::*Field);
	TSharedRef<SWidget> MakeToggleRow(const FText& Label, bool UAmplitudeUserSettings::*Field);
	TSharedRef<SWidget> MakeControlsGrid();

	void ChangeResolution(int32 Direction);
	void ChangeFrameRateLimit(int32 Direction);
	void ApplyGraphics();
	void BindLaneKey(int32 Lane, int32 Slot, const FKey& Key);
	void BindPauseKey(int32 Slot, const FKey& Key);
	static void RemoveKeyEverywhere(FAmplitudeControlProfile& Profile, const FKey& Key);
	void OnControlsChanged();
	void RestoreDefaults();

	TArray<FIntPoint> Resolutions;
};
