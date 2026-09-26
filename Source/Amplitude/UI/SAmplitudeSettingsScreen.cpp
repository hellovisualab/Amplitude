#include "UI/SAmplitudeSettingsScreen.h"

#include "Core/AmpTypes.h"
#include "Game/AmplitudeDirector.h"
#include "Game/AmplitudeUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "AmplitudeSettings"

namespace
{
	constexpr float VolumeStep = 0.05f;
	constexpr int32 OffsetStepMs = 5;
	constexpr int32 MaxOffsetMs = 500;
	const float FrameRateLimits[] = {30.0f, 60.0f, 120.0f, 144.0f, 240.0f, 0.0f};

	FText OnOff(bool bValue)
	{
		return bValue ? LOCTEXT("On", "ON") : LOCTEXT("Off", "OFF");
	}

	FText ColorblindName(EAmplitudeColorblindMode Mode)
	{
		switch (Mode)
		{
		case EAmplitudeColorblindMode::Deuteranopia:
			return LOCTEXT("Deuteranopia", "Deuteranopia");
		case EAmplitudeColorblindMode::Protanopia:
			return LOCTEXT("Protanopia", "Protanopia");
		case EAmplitudeColorblindMode::Tritanopia:
			return LOCTEXT("Tritanopia", "Tritanopia");
		default:
			return LOCTEXT("ColorblindOff", "OFF");
		}
	}

	FText ControllerName(EAmplitudeControllerMode Mode)
	{
		switch (Mode)
		{
		case EAmplitudeControllerMode::Keyboard:
			return LOCTEXT("Keyboard", "Keyboard");
		case EAmplitudeControllerMode::Gamepad:
			return LOCTEXT("Gamepad", "Gamepad");
		default:
			return LOCTEXT("AutoDetect", "Auto-Detect");
		}
	}

	template <typename EnumType>
	EnumType CycleEnum(EnumType Value, int32 Direction, int32 Count)
	{
		return static_cast<EnumType>((static_cast<int32>(Value) + Direction + Count) % Count);
	}
}

UAmplitudeUserSettings* SAmplitudeSettingsScreen::GetSettings()
{
	return UAmplitudeUserSettings::Get();
}

void SAmplitudeSettingsScreen::Construct(const FArguments& InArgs)
{
	Director = InArgs._Director;

	UKismetSystemLibrary::GetSupportedFullscreenResolutions(Resolutions);
	if (Resolutions.Num() == 0)
	{
		Resolutions = {FIntPoint(1280, 720), FIntPoint(1600, 900), FIntPoint(1920, 1080), FIntPoint(2560, 1440), FIntPoint(3840, 2160)};
	}

	TSharedPtr<SWidget> FirstRow;
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	auto AddRow = [&Rows](const TSharedRef<SWidget>& Row)
	{
		Rows->AddSlot().AutoHeight().Padding(0.0f, 3.0f)[Row];
	};

	// ---- Audio
	AddRow(AmplitudeUI::MakeSectionHeader(LOCTEXT("Audio", "AUDIO")));
	const TSharedRef<SWidget> MasterRow = MakeVolumeRow(LOCTEXT("MasterVolume", "Master Volume"), &UAmplitudeUserSettings::MasterVolume);
	FirstRow = MasterRow;
	AddRow(MasterRow);
	AddRow(MakeVolumeRow(LOCTEXT("MusicVolume", "Music Volume"), &UAmplitudeUserSettings::MusicVolume));
	AddRow(MakeVolumeRow(LOCTEXT("SfxVolume", "SFX Volume"), &UAmplitudeUserSettings::SfxVolume));
	AddRow(SNew(SAmplitudeOptionRow)
		.Label(LOCTEXT("AudioOffset", "Audio Offset"))
		.Value_Lambda([]()
		{
			const UAmplitudeUserSettings* Settings = GetSettings();
			return FText::FromString(FString::Printf(TEXT("%+d ms"), Settings != nullptr ? Settings->AudioOffsetMs : 0));
		})
		.OnDecrement_Lambda([]()
		{
			if (UAmplitudeUserSettings* Settings = GetSettings())
			{
				Settings->AudioOffsetMs = FMath::Clamp(Settings->AudioOffsetMs - OffsetStepMs, -MaxOffsetMs, MaxOffsetMs);
			}
		})
		.OnIncrement_Lambda([]()
		{
			if (UAmplitudeUserSettings* Settings = GetSettings())
			{
				Settings->AudioOffsetMs = FMath::Clamp(Settings->AudioOffsetMs + OffsetStepMs, -MaxOffsetMs, MaxOffsetMs);
			}
		}));
	AddRow(SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(18.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(300.0f)
			[
				AmplitudeUI::MakeText(LOCTEXT("OffsetInput", "Offset input (ms)"), 17.0f, AmplitudeStyle::TextDim, true)
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(140.0f)
			[
				SNew(SEditableTextBox)
				.Font(AmplitudeStyle::Font(17.0f, false))
				.Text_Lambda([]()
				{
					const UAmplitudeUserSettings* Settings = GetSettings();
					return FText::FromString(FString::FromInt(Settings != nullptr ? Settings->AudioOffsetMs : 0));
				})
				.OnTextCommitted_Lambda([](const FText& Text, ETextCommit::Type)
				{
					UAmplitudeUserSettings* Settings = GetSettings();
					const FString Value = Text.ToString().TrimStartAndEnd();
					if (Settings != nullptr && Value.IsNumeric())
					{
						Settings->AudioOffsetMs = FMath::Clamp(FCString::Atoi(*Value), -MaxOffsetMs, MaxOffsetMs);
					}
				})
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		.Padding(16.0f, 0.0f)
		[
			AmplitudeUI::MakeText(LOCTEXT("OffsetHint", "Raise it if notes feel early compared with the music. Applies from the next song."), 14.0f, AmplitudeStyle::TextDim)
		]);

	// ---- Graphics
	AddRow(AmplitudeUI::MakeSectionHeader(LOCTEXT("Graphics", "GRAPHICS")));
	AddRow(SNew(SAmplitudeOptionRow)
		.Label(LOCTEXT("Resolution", "Resolution"))
		.Value_Lambda([]()
		{
			const UAmplitudeUserSettings* Settings = GetSettings();
			const FIntPoint Resolution = Settings != nullptr ? Settings->GetScreenResolution() : FIntPoint::ZeroValue;
			return FText::FromString(FString::Printf(TEXT("%dx%d"), Resolution.X, Resolution.Y));
		})
		.OnDecrement_Lambda([this]() { ChangeResolution(-1); })
		.OnIncrement_Lambda([this]() { ChangeResolution(1); }));
	AddRow(SNew(SAmplitudeOptionRow)
		.Label(LOCTEXT("Fullscreen", "Fullscreen"))
		.Value_Lambda([]()
		{
			const UAmplitudeUserSettings* Settings = GetSettings();
			return OnOff(Settings != nullptr && Settings->GetFullscreenMode() != EWindowMode::Windowed);
		})
		.OnDecrement_Lambda([this]()
		{
			if (UAmplitudeUserSettings* Settings = GetSettings())
			{
				const bool bFullscreen = Settings->GetFullscreenMode() != EWindowMode::Windowed;
				Settings->SetFullscreenMode(bFullscreen ? EWindowMode::Windowed : EWindowMode::WindowedFullscreen);
				ApplyGraphics();
			}
		})
		.OnIncrement_Lambda([this]()
		{
			if (UAmplitudeUserSettings* Settings = GetSettings())
			{
				const bool bFullscreen = Settings->GetFullscreenMode() != EWindowMode::Windowed;
				Settings->SetFullscreenMode(bFullscreen ? EWindowMode::Windowed : EWindowMode::WindowedFullscreen);
				ApplyGraphics();
			}
		}));
	AddRow(SNew(SAmplitudeOptionRow)
		.Label(LOCTEXT("FrameRateCap", "Frame Rate Cap"))
		.Value_Lambda([]()
		{
			const UAmplitudeUserSettings* Settings = GetSettings();
			const float Limit = Settings != nullptr ? Settings->GetFrameRateLimit() : 0.0f;
			return Limit > 0.0f ? FText::FromString(FString::Printf(TEXT("%.0f FPS"), Limit)) : LOCTEXT("Unlimited", "Unlimited");
		})
		.OnDecrement_Lambda([this]() { ChangeFrameRateLimit(-1); })
		.OnIncrement_Lambda([this]() { ChangeFrameRateLimit(1); }));
	AddRow(SNew(SAmplitudeOptionRow)
		.Label(LOCTEXT("VSync", "V-Sync"))
		.Value_Lambda([]()
		{
			const UAmplitudeUserSettings* Settings = GetSettings();
			return OnOff(Settings != nullptr && Settings->IsVSyncEnabled());
		})
		.OnIncrement_Lambda([this]()
		{
			if (UAmplitudeUserSettings* Settings = GetSettings())
			{
				Settings->SetVSyncEnabled(!Settings->IsVSyncEnabled());
				ApplyGraphics();
			}
		})
		.OnDecrement_Lambda([this]()
		{
			if (UAmplitudeUserSettings* Settings = GetSettings())
			{
				Settings->SetVSyncEnabled(!Settings->IsVSyncEnabled());
				ApplyGraphics();
			}
		}));

	// ---- Gameplay
	AddRow(AmplitudeUI::MakeSectionHeader(LOCTEXT("Gameplay", "GAMEPLAY")));
	AddRow(MakeToggleRow(LOCTEXT("ShowCombo", "Show Combo"), &UAmplitudeUserSettings::bShowCombo));
	AddRow(MakeToggleRow(LOCTEXT("ShowEnergy", "Show Energy"), &UAmplitudeUserSettings::bShowEnergy));
	AddRow(MakeToggleRow(LOCTEXT("ShowStats", "Performance Stats"), &UAmplitudeUserSettings::bShowPerformanceStats));
	AddRow(SNew(SAmplitudeOptionRow)
		.Label(LOCTEXT("Colorblind", "Colorblind Mode"))
		.Value_Lambda([]()
		{
			const UAmplitudeUserSettings* Settings = GetSettings();
			return ColorblindName(Settings != nullptr ? Settings->ColorblindMode : EAmplitudeColorblindMode::Off);
		})
		.OnDecrement_Lambda([]()
		{
			if (UAmplitudeUserSettings* Settings = GetSettings())
			{
				Settings->ColorblindMode = CycleEnum(Settings->ColorblindMode, -1, 4);
			}
		})
		.OnIncrement_Lambda([]()
		{
			if (UAmplitudeUserSettings* Settings = GetSettings())
			{
				Settings->ColorblindMode = CycleEnum(Settings->ColorblindMode, 1, 4);
			}
		}));
	AddRow(SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(18.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(300.0f)
			[
				AmplitudeUI::MakeText(LOCTEXT("PlayerName", "Player Name"), 17.0f, AmplitudeStyle::TextDim, true)
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(320.0f)
			[
				SNew(SEditableTextBox)
				.Font(AmplitudeStyle::Font(17.0f, false))
				.Text_Lambda([]()
				{
					const UAmplitudeUserSettings* Settings = GetSettings();
					return FText::FromString(Settings != nullptr ? Settings->PlayerName : FString());
				})
				.OnTextCommitted_Lambda([](const FText& Text, ETextCommit::Type)
				{
					UAmplitudeUserSettings* Settings = GetSettings();
					const FString Name = Text.ToString().TrimStartAndEnd().Left(24);
					if (Settings != nullptr && !Name.IsEmpty())
					{
						Settings->PlayerName = Name;
					}
				})
			]
		]);

	// ---- Controls
	AddRow(AmplitudeUI::MakeSectionHeader(LOCTEXT("Controls", "CONTROLS")));
	AddRow(SNew(SAmplitudeOptionRow)
		.Label(LOCTEXT("Controller", "Controller"))
		.Value_Lambda([]()
		{
			const UAmplitudeUserSettings* Settings = GetSettings();
			return ControllerName(Settings != nullptr ? Settings->ControllerMode : EAmplitudeControllerMode::AutoDetect);
		})
		.OnDecrement_Lambda([]()
		{
			if (UAmplitudeUserSettings* Settings = GetSettings())
			{
				Settings->ControllerMode = CycleEnum(Settings->ControllerMode, -1, 3);
			}
		})
		.OnIncrement_Lambda([]()
		{
			if (UAmplitudeUserSettings* Settings = GetSettings())
			{
				Settings->ControllerMode = CycleEnum(Settings->ControllerMode, 1, 3);
			}
		}));
	AddRow(SNew(SAmplitudeOptionRow)
		.Label(LOCTEXT("Profile", "Control Profile"))
		.Value_Lambda([]()
		{
			const UAmplitudeUserSettings* Settings = GetSettings();
			if (Settings == nullptr)
			{
				return FText::GetEmpty();
			}
			return FText::FromString(FString::Printf(TEXT("%s  (%d/%d)"), *Settings->GetActiveProfile().Name, Settings->ActiveProfileIndex + 1, Settings->ControlProfiles.Num()));
		})
		.OnDecrement_Lambda([this]()
		{
			if (UAmplitudeUserSettings* Settings = GetSettings())
			{
				const int32 Count = FMath::Max(1, Settings->ControlProfiles.Num());
				Settings->ActiveProfileIndex = (Settings->ActiveProfileIndex - 1 + Count) % Count;
				OnControlsChanged();
			}
		})
		.OnIncrement_Lambda([this]()
		{
			if (UAmplitudeUserSettings* Settings = GetSettings())
			{
				const int32 Count = FMath::Max(1, Settings->ControlProfiles.Num());
				Settings->ActiveProfileIndex = (Settings->ActiveProfileIndex + 1) % Count;
				OnControlsChanged();
			}
		}));
	AddRow(SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 4.0f, 12.0f, 4.0f)
		[
			SNew(SAmplitudeButton)
			.Text(LOCTEXT("SaveProfile", "Save As New Profile"))
			.MinWidth(260.0f)
			.FontSize(16.0f)
			.OnClicked_Lambda([this]()
			{
				if (UAmplitudeUserSettings* Settings = GetSettings())
				{
					Settings->AddProfileFromActive();
					OnControlsChanged();
				}
			})
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 4.0f, 12.0f, 4.0f)
		[
			SNew(SAmplitudeButton)
			.Text(LOCTEXT("DeleteProfile", "Delete Profile"))
			.MinWidth(220.0f)
			.FontSize(16.0f)
			.OnClicked_Lambda([this]()
			{
				if (UAmplitudeUserSettings* Settings = GetSettings())
				{
					Settings->RemoveActiveProfile();
					OnControlsChanged();
				}
			})
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 4.0f)
		[
			SNew(SAmplitudeButton)
			.Text(LOCTEXT("ResetProfile", "Reset Bindings"))
			.MinWidth(220.0f)
			.FontSize(16.0f)
			.OnClicked_Lambda([this]()
			{
				if (UAmplitudeUserSettings* Settings = GetSettings())
				{
					FAmplitudeControlProfile& Profile = Settings->GetMutableActiveProfile();
					const FString Name = Profile.Name;
					Profile = FAmplitudeControlProfile::MakeDefault();
					Profile.Name = Name;
					OnControlsChanged();
				}
			})
		]);
	AddRow(MakeControlsGrid());

	ChildSlot
	[
		AmplitudeUI::MakeBackdrop(
			AmplitudeUI::MakePanel(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(0.0f, 0.0f, 0.0f, 10.0f)
				[
					AmplitudeUI::MakeTitle(LOCTEXT("SettingsTitle", "SETTINGS"), 48.0f)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.WidthOverride(1120.0f)
					.HeightOverride(640.0f)
					[
						SNew(SScrollBox)
						.ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll)
						+ SScrollBox::Slot()
						.Padding(0.0f, 0.0f, 16.0f, 0.0f)
						[
							Rows
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(0.0f, 18.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(10.0f, 0.0f)
					[
						SNew(SAmplitudeButton)
						.Text(LOCTEXT("Back", "Back"))
						.MinWidth(220.0f)
						.OnClicked_Lambda([this]() { OnBack(); })
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(10.0f, 0.0f)
					[
						SNew(SAmplitudeButton)
						.Text(LOCTEXT("Defaults", "Defaults"))
						.MinWidth(220.0f)
						.OnClicked_Lambda([this]() { RestoreDefaults(); })
					]
				]),
			0.7f)
	];

	InitialFocus = FirstRow;
}

TSharedRef<SWidget> SAmplitudeSettingsScreen::MakeVolumeRow(const FText& Label, float UAmplitudeUserSettings::*Field)
{
	return SNew(SAmplitudeOptionRow)
		.Label(Label)
		.BarFraction_Lambda([Field]()
		{
			const UAmplitudeUserSettings* Settings = GetSettings();
			return Settings != nullptr ? TOptional<float>(Settings->*Field) : TOptional<float>(0.0f);
		})
		.Value_Lambda([Field]()
		{
			const UAmplitudeUserSettings* Settings = GetSettings();
			return FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt32((Settings != nullptr ? Settings->*Field : 0.0f) * 100.0f)));
		})
		.OnDecrement_Lambda([this, Field]()
		{
			if (UAmplitudeUserSettings* Settings = GetSettings())
			{
				Settings->*Field = FMath::Clamp(FMath::RoundToFloat((Settings->*Field - VolumeStep) * 20.0f) / 20.0f, 0.0f, 1.0f);
				if (AAmplitudeDirector* Owner = GetDirector())
				{
					Owner->ApplyAudioSettings();
				}
			}
		})
		.OnIncrement_Lambda([this, Field]()
		{
			if (UAmplitudeUserSettings* Settings = GetSettings())
			{
				Settings->*Field = FMath::Clamp(FMath::RoundToFloat((Settings->*Field + VolumeStep) * 20.0f) / 20.0f, 0.0f, 1.0f);
				if (AAmplitudeDirector* Owner = GetDirector())
				{
					Owner->ApplyAudioSettings();
				}
			}
		});
}

TSharedRef<SWidget> SAmplitudeSettingsScreen::MakeToggleRow(const FText& Label, bool UAmplitudeUserSettings::*Field)
{
	auto Toggle = [Field]()
	{
		if (UAmplitudeUserSettings* Settings = GetSettings())
		{
			Settings->*Field = !(Settings->*Field);
		}
	};
	return SNew(SAmplitudeOptionRow)
		.Label(Label)
		.Value_Lambda([Field]()
		{
			const UAmplitudeUserSettings* Settings = GetSettings();
			return OnOff(Settings != nullptr && Settings->*Field);
		})
		.OnDecrement_Lambda(Toggle)
		.OnIncrement_Lambda(Toggle);
}

TSharedRef<SWidget> SAmplitudeSettingsScreen::MakeControlsGrid()
{
	constexpr float LabelWidth = 190.0f;
	constexpr float CellWidth = 200.0f;
	const FText SlotNames[AmplitudeControls::NumSlots] = {LOCTEXT("Primary", "PRIMARY"), LOCTEXT("Alt1", "ALT 1"), LOCTEXT("Alt2", "ALT 2"), LOCTEXT("GamepadSlot", "GAMEPAD")};

	TSharedRef<SVerticalBox> Grid = SNew(SVerticalBox);

	TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SBox)
			.WidthOverride(LabelWidth)
		];
	for (int32 SlotIndex = 0; SlotIndex < AmplitudeControls::NumSlots; ++SlotIndex)
	{
		Header->AddSlot()
		.AutoWidth()
		.Padding(6.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(CellWidth)
			[
				AmplitudeUI::MakeText(SlotNames[SlotIndex], 15.0f, AmplitudeStyle::Accent, true)
			]
		];
	}
	Grid->AddSlot().AutoHeight().Padding(18.0f, 10.0f, 0.0f, 6.0f)[Header];

	for (int32 Row = 0; Row <= Amp::NumLanes; ++Row)
	{
		const bool bPauseRow = Row == Amp::NumLanes;
		const FText RowLabel = bPauseRow
			? LOCTEXT("Pause", "PAUSE")
			: FText::FromString(FString::Printf(TEXT("%d  %s"), Row + 1, *AmplitudeStyle::GetLaneLabel(Row).ToString()));
		const FLinearColor RowColor = bPauseRow ? AmplitudeStyle::Text : AmplitudeStyle::GetLaneColor(Row);

		TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(LabelWidth)
				[
					AmplitudeUI::MakeText(RowLabel, 16.0f, RowColor, true)
				]
			];
		for (int32 SlotIndex = 0; SlotIndex < AmplitudeControls::NumSlots; ++SlotIndex)
		{
			const bool bGamepadSlot = SlotIndex == AmplitudeControls::GamepadSlot;
			Line->AddSlot()
			.AutoWidth()
			.Padding(6.0f, 0.0f)
			[
				SNew(SAmplitudeKeyBinder)
				.Width(CellWidth)
				.bGamepad(bGamepadSlot)
				.Key_Lambda([Row, SlotIndex, bPauseRow]()
				{
					const UAmplitudeUserSettings* Settings = GetSettings();
					if (Settings == nullptr)
					{
						return EKeys::Invalid;
					}
					return bPauseRow ? Settings->GetActiveProfile().GetPauseKey(SlotIndex) : Settings->GetActiveProfile().GetLaneKey(Row, SlotIndex);
				})
				.OnKeyChosen_Lambda([this, Row, SlotIndex, bPauseRow](FKey Key)
				{
					if (bPauseRow)
					{
						BindPauseKey(SlotIndex, Key);
					}
					else
					{
						BindLaneKey(Row, SlotIndex, Key);
					}
				})
			];
		}
		Grid->AddSlot().AutoHeight().Padding(18.0f, 3.0f, 0.0f, 3.0f)[Line];
	}

	Grid->AddSlot()
	.AutoHeight()
	.Padding(18.0f, 10.0f, 0.0f, 0.0f)
	[
		AmplitudeUI::MakeText(LOCTEXT("RebindHint", "Select a cell and press Enter (or click) then the new key. Esc cancels, Delete clears. A key can only be bound once."), 14.0f, AmplitudeStyle::TextDim)
	];
	return Grid;
}

void SAmplitudeSettingsScreen::ChangeResolution(int32 Direction)
{
	UAmplitudeUserSettings* Settings = GetSettings();
	if (Settings == nullptr || Resolutions.Num() == 0)
	{
		return;
	}
	const FIntPoint Current = Settings->GetScreenResolution();
	int32 Index = Resolutions.IndexOfByKey(Current);
	if (Index == INDEX_NONE)
	{
		// Snap to the closest listed resolution first.
		Index = 0;
		for (int32 Candidate = 0; Candidate < Resolutions.Num(); ++Candidate)
		{
			if (Resolutions[Candidate].X * Resolutions[Candidate].Y <= Current.X * Current.Y)
			{
				Index = Candidate;
			}
		}
	}
	Index = (Index + Direction + Resolutions.Num()) % Resolutions.Num();
	Settings->SetScreenResolution(Resolutions[Index]);
	ApplyGraphics();
}

void SAmplitudeSettingsScreen::ChangeFrameRateLimit(int32 Direction)
{
	UAmplitudeUserSettings* Settings = GetSettings();
	if (Settings == nullptr)
	{
		return;
	}
	const int32 Count = static_cast<int32>(UE_ARRAY_COUNT(FrameRateLimits));
	int32 Index = 1;
	for (int32 Candidate = 0; Candidate < Count; ++Candidate)
	{
		if (FMath::IsNearlyEqual(FrameRateLimits[Candidate], Settings->GetFrameRateLimit()))
		{
			Index = Candidate;
		}
	}
	Index = (Index + Direction + Count) % Count;
	Settings->SetFrameRateLimit(FrameRateLimits[Index]);
	ApplyGraphics();
}

void SAmplitudeSettingsScreen::ApplyGraphics()
{
	if (UAmplitudeUserSettings* Settings = GetSettings())
	{
		Settings->ApplySettings(false);
	}
}

void SAmplitudeSettingsScreen::RemoveKeyEverywhere(FAmplitudeControlProfile& Profile, const FKey& Key)
{
	for (FAmplitudeLaneBinding& Binding : Profile.Lanes)
	{
		for (FKey& Bound : Binding.Keys)
		{
			if (Bound == Key)
			{
				Bound = EKeys::Invalid;
			}
		}
	}
	for (FKey& Bound : Profile.PauseKeys)
	{
		if (Bound == Key)
		{
			Bound = EKeys::Invalid;
		}
	}
}

void SAmplitudeSettingsScreen::BindLaneKey(int32 Lane, int32 Slot, const FKey& Key)
{
	if (UAmplitudeUserSettings* Settings = GetSettings())
	{
		FAmplitudeControlProfile& Profile = Settings->GetMutableActiveProfile();
		if (Key.IsValid())
		{
			RemoveKeyEverywhere(Profile, Key);
		}
		Profile.SetLaneKey(Lane, Slot, Key);
		OnControlsChanged();
	}
}

void SAmplitudeSettingsScreen::BindPauseKey(int32 Slot, const FKey& Key)
{
	if (UAmplitudeUserSettings* Settings = GetSettings())
	{
		FAmplitudeControlProfile& Profile = Settings->GetMutableActiveProfile();
		if (Key.IsValid())
		{
			RemoveKeyEverywhere(Profile, Key);
		}
		Profile.SetPauseKey(Slot, Key);
		OnControlsChanged();
	}
}

void SAmplitudeSettingsScreen::OnControlsChanged()
{
	if (AAmplitudeDirector* Owner = GetDirector())
	{
		Owner->ApplyControlSettings();
	}
}

void SAmplitudeSettingsScreen::RestoreDefaults()
{
	UAmplitudeUserSettings* Settings = GetSettings();
	AAmplitudeDirector* Owner = GetDirector();
	if (Settings == nullptr || Owner == nullptr)
	{
		return;
	}
	Settings->SetToDefaults();
	Settings->ApplySettings(false);
	Owner->ApplyAudioSettings();
	Owner->ApplyControlSettings();
	// Rebuild the screen so text boxes pick up the default values.
	Owner->ShowSettings();
}

void SAmplitudeSettingsScreen::OnBack()
{
	if (AAmplitudeDirector* Owner = GetDirector())
	{
		Owner->ApplyAudioSettings();
		Owner->CloseSettings();
	}
}

#undef LOCTEXT_NAMESPACE
