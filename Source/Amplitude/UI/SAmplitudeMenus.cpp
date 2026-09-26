#include "UI/SAmplitudeMenus.h"

#include "Core/AmpRules.h"
#include "Data/AmplitudeLeaderboard.h"
#include "Data/AmplitudeSongLibrary.h"
#include "Framework/Application/SlateApplication.h"
#include "Game/AmplitudeDirector.h"
#include "Game/AmplitudeSession.h"
#include "Game/AmplitudeUserSettings.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "AmplitudeMenus"

namespace
{
	FText DifficultyText(Amp::EDifficulty Difficulty)
	{
		return FText::FromString(ANSI_TO_TCHAR(Amp::GetDifficultyName(Difficulty)));
	}

	FText LaneText(int32 Lane)
	{
		return FText::Format(LOCTEXT("LaneFormat", "Lane {0} ({1})"), FText::AsNumber(Lane + 1), AmplitudeStyle::GetLaneLabel(Lane));
	}

	FText Percent(double Value)
	{
		return FText::FromString(FString::Printf(TEXT("%.1f%%"), Value));
	}

	/** Row of six coloured bars in the lane palette, used as a logo accent. */
	TSharedRef<SWidget> MakeLaneStrip(float SegmentWidth, float Height)
	{
		TSharedRef<SHorizontalBox> Strip = SNew(SHorizontalBox);
		for (int32 Lane = 0; Lane < Amp::NumLanes; ++Lane)
		{
			Strip->AddSlot()
			.AutoWidth()
			.Padding(3.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(SegmentWidth)
				.HeightOverride(Height)
				[
					SNew(SImage)
					.Image(AmplitudeStyle::RoundedBrush())
					.ColorAndOpacity_Lambda([Lane]() { return FSlateColor(AmplitudeStyle::GetLaneColor(Lane)); })
				]
			];
		}
		return Strip;
	}

	/** Label/value line used in statistics blocks. */
	TSharedRef<SWidget> MakeStatLine(const FText& Label, const TAttribute<FText>& Value, const FLinearColor& ValueColor = AmplitudeStyle::Text)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(260.0f)
				[
					AmplitudeUI::MakeText(Label, 18.0f, AmplitudeStyle::TextDim, true)
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				AmplitudeUI::MakeText(Value, 18.0f, ValueColor, true)
			];
	}
}

// ---------------------------------------------------------------------------------------------
// Main menu

void SAmplitudeMainMenu::Construct(const FArguments& InArgs)
{
	Director = InArgs._Director;

	TSharedPtr<SAmplitudeButton> PlayButton;
	const AAmplitudeDirector* Owner = GetDirector();
	const int32 SongCount = Owner != nullptr ? Owner->GetSongLibrary().Num() : 0;
	const FText LibraryText = SongCount > 0
		? FText::Format(LOCTEXT("SongsFound", "{0} song(s) found"), FText::AsNumber(SongCount))
		: LOCTEXT("NoSongsHint", "No songs found - run Tools/generate_demo_song.py (see README)");

	ChildSlot
	[
		AmplitudeUI::MakeBackdrop(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				MakeLaneStrip(64.0f, 6.0f)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				AmplitudeUI::MakeTitle(LOCTEXT("Title", "AMPLITUDE"), 96.0f)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 0.0f, 0.0f, 40.0f)
			[
				AmplitudeUI::MakeText(LOCTEXT("Subtitle", "A Rhythm-Action Game"), 24.0f, AmplitudeStyle::TextDim)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 6.0f)
			[
				SAssignNew(PlayButton, SAmplitudeButton)
				.Text(LOCTEXT("Play", "Play Game"))
				.OnClicked_Lambda([this]() { if (AAmplitudeDirector* D = GetDirector()) { D->ShowSongSelect(); } })
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 6.0f)
			[
				SNew(SAmplitudeButton)
				.Text(LOCTEXT("Settings", "Settings"))
				.OnClicked_Lambda([this]() { if (AAmplitudeDirector* D = GetDirector()) { D->ShowSettings(); } })
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 6.0f)
			[
				SNew(SAmplitudeButton)
				.Text(LOCTEXT("Leaderboards", "Leaderboards"))
				.OnClicked_Lambda([this]() { if (AAmplitudeDirector* D = GetDirector()) { D->ShowLeaderboards(); } })
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 6.0f)
			[
				SNew(SAmplitudeButton)
				.Text(LOCTEXT("Credits", "Credits"))
				.OnClicked_Lambda([this]() { if (AAmplitudeDirector* D = GetDirector()) { D->ShowCredits(); } })
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 6.0f)
			[
				SNew(SAmplitudeButton)
				.Text(LOCTEXT("Exit", "Exit"))
				.OnClicked_Lambda([this]() { if (AAmplitudeDirector* D = GetDirector()) { D->QuitGame(); } })
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 36.0f, 0.0f, 0.0f)
			[
				AmplitudeUI::MakeText(LibraryText, 16.0f, SongCount > 0 ? AmplitudeStyle::TextDim : AmplitudeStyle::Fever)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				AmplitudeUI::MakeText(LOCTEXT("Version", "Version 1.0"), 14.0f, AmplitudeStyle::TextDim)
			],
			0.45f)
	];

	InitialFocus = PlayButton->GetFocusTarget();
}

// ---------------------------------------------------------------------------------------------
// Song select

void SAmplitudeSongSelect::Construct(const FArguments& InArgs)
{
	Director = InArgs._Director;
	const AAmplitudeDirector* Owner = GetDirector();
	if (Owner == nullptr || Owner->GetSongLibrary().Num() == 0)
	{
		ChildSlot
		[
			AmplitudeUI::MakeBackdrop(MakeEmptyLibraryContent(), 0.6f)
		];
		return;
	}

	TSharedRef<SVerticalBox> SongList = SNew(SVerticalBox);
	TSharedPtr<SWidget> SelectedSongFocus;
	const TArray<TSharedPtr<const FAmplitudeSongDefinition>>& Songs = Owner->GetSongLibrary().GetSongs();
	for (int32 Index = 0; Index < Songs.Num(); ++Index)
	{
		const FAmplitudeSongDefinition& Song = *Songs[Index];
		const FString Label = Song.Artist.IsEmpty() ? Song.Title : FString::Printf(TEXT("%s - %s"), *Song.Title, *Song.Artist);
		TSharedPtr<SAmplitudeButton> SongButton;
		SongList->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 4.0f)
		[
			SAssignNew(SongButton, SAmplitudeButton)
			.Text(FText::FromString(Label))
			.MinWidth(520.0f)
			.FontSize(18.0f)
			.IsSelected_Lambda([this, Index]()
			{
				const AAmplitudeDirector* D = GetDirector();
				return D != nullptr && D->GetSelectedSongIndex() == Index;
			})
			.OnFocused_Lambda([this, Index]() { if (AAmplitudeDirector* D = GetDirector()) { D->SelectSong(Index); } })
			.OnClicked_Lambda([this, Index]()
			{
				if (AAmplitudeDirector* D = GetDirector())
				{
					D->SelectSong(Index);
				}
				// Choosing a song moves on to the Play button so a second confirm starts it.
				if (PlayButton.IsValid() && FSlateApplication::IsInitialized())
				{
					FSlateApplication::Get().SetAllUserFocus(PlayButton->GetFocusTarget(), EFocusCause::Navigation);
				}
			})
		];
		if (Index == Owner->GetSelectedSongIndex())
		{
			SelectedSongFocus = SongButton->GetFocusTarget();
		}
	}

	ChildSlot
	[
		AmplitudeUI::MakeBackdrop(
			AmplitudeUI::MakePanel(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(0.0f, 0.0f, 0.0f, 24.0f)
				[
					AmplitudeUI::MakeTitle(LOCTEXT("SelectSong", "SELECT SONG"), 48.0f)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SBox)
						.WidthOverride(560.0f)
						.HeightOverride(520.0f)
						[
							SNew(SScrollBox)
							.ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll)
							+ SScrollBox::Slot()
							[
								SongList
							]
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(40.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(640.0f)
						[
							MakeDetails()
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(0.0f, 28.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(10.0f, 0.0f)
					[
						SAssignNew(PlayButton, SAmplitudeButton)
						.Text(LOCTEXT("PlaySong", "Play"))
						.MinWidth(220.0f)
						.OnClicked_Lambda([this]() { if (AAmplitudeDirector* D = GetDirector()) { D->StartSelectedSong(); } })
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(10.0f, 0.0f)
					[
						SNew(SAmplitudeButton)
						.Text(LOCTEXT("Back", "Back"))
						.MinWidth(220.0f)
						.OnClicked_Lambda([this]() { OnBack(); })
					]
				]),
			0.55f)
	];

	InitialFocus = SelectedSongFocus.IsValid() ? SelectedSongFocus : PlayButton->GetFocusTarget();
}

TSharedRef<SWidget> SAmplitudeSongSelect::MakeEmptyLibraryContent()
{
	const AAmplitudeDirector* Owner = GetDirector();
	FString Directories;
	FString Errors;
	if (Owner != nullptr)
	{
		Directories = FString::Join(Owner->GetSongLibrary().GetScannedDirectories(), TEXT("\n"));
		Errors = FString::Join(Owner->GetSongLibrary().GetErrors(), TEXT("\n"));
	}

	TSharedPtr<SAmplitudeButton> RescanButton;
	TSharedRef<SWidget> Content = AmplitudeUI::MakePanel(
		SNew(SBox)
		.WidthOverride(900.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				AmplitudeUI::MakeTitle(LOCTEXT("NoSongs", "NO SONGS FOUND"), 44.0f, AmplitudeStyle::Fever)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 20.0f, 0.0f, 0.0f)
			[
				AmplitudeUI::MakeText(FText::Format(LOCTEXT("NoSongsBody",
					"Songs are folders containing a song.json chart and multitrack WAV audio, placed in:\n{0}\n\n"
					"Generate the bundled demo song with:  python3 Tools/generate_demo_song.py\n"
					"then press Rescan. See README.md for the song format."), FText::FromString(Directories)), 17.0f)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 16.0f, 0.0f, 0.0f)
			[
				AmplitudeUI::MakeText(FText::FromString(Errors), 15.0f, AmplitudeStyle::Miss)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 28.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(10.0f, 0.0f)
				[
					SAssignNew(RescanButton, SAmplitudeButton)
					.Text(LOCTEXT("Rescan", "Rescan"))
					.MinWidth(220.0f)
					.OnClicked_Lambda([this]()
					{
						if (AAmplitudeDirector* D = GetDirector())
						{
							D->RescanSongs();
							D->ShowSongSelect();
						}
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(10.0f, 0.0f)
				[
					SNew(SAmplitudeButton)
					.Text(LOCTEXT("Back", "Back"))
					.MinWidth(220.0f)
					.OnClicked_Lambda([this]() { OnBack(); })
				]
			]
		]);
	InitialFocus = RescanButton->GetFocusTarget();
	return Content;
}

TSharedRef<SWidget> SAmplitudeSongSelect::MakeDetails()
{
	TSharedRef<SHorizontalBox> Stars = SNew(SHorizontalBox);
	for (int32 Star = 0; Star < 5; ++Star)
	{
		Stars->AddSlot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(22.0f)
			.HeightOverride(22.0f)
			[
				SNew(SImage)
				.Image(AmplitudeStyle::RoundedBrush())
				.ColorAndOpacity_Lambda([this, Star]()
				{
					const AAmplitudeDirector* D = GetDirector();
					const TSharedPtr<const FAmplitudeSongDefinition> Song = D != nullptr ? D->GetSelectedSong() : nullptr;
					const int32 Rating = Song.IsValid() ? Song->GetStarRating() : 0;
					return FSlateColor(Star < Rating ? AmplitudeStyle::Perfect : FLinearColor(1.0f, 1.0f, 1.0f, 0.12f));
				})
			]
		];
	}

	TSharedRef<SHorizontalBox> Difficulties = SNew(SHorizontalBox);
	for (int32 Index = 0; Index < Amp::NumDifficulties; ++Index)
	{
		const Amp::EDifficulty Difficulty = static_cast<Amp::EDifficulty>(Index);
		FText Label = DifficultyText(Difficulty);
		if (Difficulty == Amp::EDifficulty::Insane)
		{
			Label = FText::FromString(Label.ToString().ToUpper());
		}
		Difficulties->AddSlot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 10.0f, 0.0f)
		[
			SNew(SAmplitudeButton)
			.Text(Label)
			.MinWidth(140.0f)
			.FontSize(17.0f)
			.AccentColor(AmplitudeStyle::GetDifficultyColor(Difficulty))
			.IsSelected_Lambda([this, Difficulty]()
			{
				const AAmplitudeDirector* D = GetDirector();
				return D != nullptr && D->GetSelectedDifficulty() == Difficulty;
			})
			.OnClicked_Lambda([this, Difficulty]() { if (AAmplitudeDirector* D = GetDirector()) { D->SelectDifficulty(Difficulty); } })
		];
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(this, &SAmplitudeSongSelect::GetDetailText, 0)
			.Font(AmplitudeStyle::Font(30.0f))
			.ColorAndOpacity(FSlateColor(AmplitudeStyle::Text))
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f)
		[
			AmplitudeUI::MakeText(TAttribute<FText>::CreateSP(this, &SAmplitudeSongSelect::GetDetailText, 1), 20.0f, AmplitudeStyle::TextDim)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 10.0f)
		[
			Stars
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f)
		[
			AmplitudeUI::MakeText(TAttribute<FText>::CreateSP(this, &SAmplitudeSongSelect::GetDetailText, 2), 18.0f)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 22.0f, 0.0f, 8.0f)
		[
			AmplitudeUI::MakeText(LOCTEXT("Difficulty", "Difficulty:"), 18.0f, AmplitudeStyle::TextDim, true)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			Difficulties
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 18.0f, 0.0f, 4.0f)
		[
			AmplitudeUI::MakeText(TAttribute<FText>::CreateSP(this, &SAmplitudeSongSelect::GetDetailText, 3), 18.0f)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f)
		[
			AmplitudeUI::MakeText(TAttribute<FText>::CreateSP(this, &SAmplitudeSongSelect::GetDetailText, 4), 20.0f, AmplitudeStyle::Perfect, true)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 16.0f, 0.0f, 0.0f)
		[
			AmplitudeUI::MakeText(TAttribute<FText>::CreateSP(this, &SAmplitudeSongSelect::GetDetailText, 5), 15.0f, AmplitudeStyle::TextDim)
		];
}

FText SAmplitudeSongSelect::GetDetailText(int32 Line) const
{
	const AAmplitudeDirector* Owner = GetDirector();
	const TSharedPtr<const FAmplitudeSongDefinition> Song = Owner != nullptr ? Owner->GetSelectedSong() : nullptr;
	if (!Song.IsValid())
	{
		return FText::GetEmpty();
	}
	const Amp::EDifficulty Difficulty = Owner->GetSelectedDifficulty();

	switch (Line)
	{
	case 0:
		return FText::FromString(Song->Title);
	case 1:
	{
		FString Text = Song->Artist.IsEmpty() ? FString(TEXT("Unknown artist")) : Song->Artist;
		if (!Song->Album.IsEmpty())
		{
			Text += FString::Printf(TEXT("  |  %s"), *Song->Album);
		}
		if (Song->Year > 0)
		{
			Text += FString::Printf(TEXT(" (%d)"), Song->Year);
		}
		return FText::FromString(Text);
	}
	case 2:
		return FText::Format(LOCTEXT("DurationBpm", "Duration: {0}    BPM: {1}    Notes: {2}"),
			AmplitudeStyle::FormatTime(Song->GetDisplayDurationMs()), FText::AsNumber(FMath::RoundToInt32(Song->Bpm)), FText::AsNumber(Song->CountNotes(Difficulty)));
	case 3:
	{
		const Amp::FDifficultyParams& Params = Song->DifficultyParams[static_cast<int32>(Difficulty)];
		return FText::FromString(FString::Printf(TEXT("Perfect +/-%.0f ms   Good +/-%.0f ms   Miss costs %d energy"),
			Params.PerfectWindowMs, Params.GoodWindowMs, -Params.EnergyOnMiss));
	}
	case 4:
	{
		const FAmplitudeScoreEntry* Best = Owner->GetLeaderboard().GetBest(Song->Id, Difficulty);
		if (Best == nullptr)
		{
			return FText::Format(LOCTEXT("NoBest", "Personal Best: - ({0})"), DifficultyText(Difficulty));
		}
		return FText::Format(LOCTEXT("Best", "Personal Best: {0} ({1})"), AmplitudeStyle::FormatScore(Best->Score), DifficultyText(Difficulty));
	}
	case 5:
		if (!Song->HasAudioConfigured())
		{
			return LOCTEXT("NoAudio", "No audio configured: the chart plays without music.");
		}
		return FText::FromString(FString::Printf(TEXT("Audio: %s"), *FPaths::GetCleanFilename(Song->AudioPath.IsEmpty() ? Song->StemPaths[0] : Song->AudioPath)));
	default:
		return FText::GetEmpty();
	}
}

void SAmplitudeSongSelect::OnBack()
{
	if (AAmplitudeDirector* Owner = GetDirector())
	{
		Owner->ShowMainMenu();
	}
}

// ---------------------------------------------------------------------------------------------
// Pause menu

void SAmplitudePauseMenu::Construct(const FArguments& InArgs)
{
	Director = InArgs._Director;

	auto SessionLine = [this](int32 Line)
	{
		return TAttribute<FText>::CreateLambda([this, Line]()
		{
			const AAmplitudeDirector* Owner = GetDirector();
			const FAmplitudeSession* Session = Owner != nullptr ? Owner->GetSession() : nullptr;
			if (Session == nullptr)
			{
				return FText::GetEmpty();
			}
			const Amp::FSimulation& Simulation = Session->GetSimulation();
			switch (Line)
			{
			case 0:
				return AmplitudeStyle::FormatScore(Simulation.GetScore());
			case 1:
				return FText::Format(LOCTEXT("PauseTime", "{0} / {1}"), AmplitudeStyle::FormatTime(FMath::Max(0.0, Session->GetSongTimeMs())), AmplitudeStyle::FormatTime(Session->GetDisplayDurationMs()));
			default:
				return FText::Format(LOCTEXT("PauseEnergy", "{0}/{1}"), FText::AsNumber(Simulation.GetEnergy()), FText::AsNumber(Simulation.GetRules().MaxEnergy));
			}
		});
	};

	TSharedPtr<SAmplitudeButton> ResumeButton;
	ChildSlot
	[
		AmplitudeUI::MakeBackdrop(
			AmplitudeUI::MakePanel(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(0.0f, 0.0f, 0.0f, 20.0f)
				[
					AmplitudeUI::MakeTitle(LOCTEXT("Paused", "PAUSED"), 56.0f)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)[MakeStatLine(LOCTEXT("PauseScore", "Score:"), SessionLine(0))]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)[MakeStatLine(LOCTEXT("PauseTimeLabel", "Time:"), SessionLine(1))]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)[MakeStatLine(LOCTEXT("PauseEnergyLabel", "Energy:"), SessionLine(2))]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(0.0f, 28.0f, 0.0f, 6.0f)
				[
					SAssignNew(ResumeButton, SAmplitudeButton)
					.Text(LOCTEXT("Resume", "Resume"))
					.OnClicked_Lambda([this]() { OnBack(); })
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(0.0f, 6.0f)
				[
					SNew(SAmplitudeButton)
					.Text(LOCTEXT("Restart", "Restart Song"))
					.OnClicked_Lambda([this]() { if (AAmplitudeDirector* D = GetDirector()) { D->RestartSong(); } })
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(0.0f, 6.0f)
				[
					SNew(SAmplitudeButton)
					.Text(LOCTEXT("PauseSettings", "Settings"))
					.OnClicked_Lambda([this]() { if (AAmplitudeDirector* D = GetDirector()) { D->ShowSettings(); } })
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(0.0f, 6.0f)
				[
					SNew(SAmplitudeButton)
					.Text(LOCTEXT("SongSelect", "Song Select"))
					.OnClicked_Lambda([this]() { if (AAmplitudeDirector* D = GetDirector()) { D->QuitToSongSelect(); } })
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(0.0f, 6.0f)
				[
					SNew(SAmplitudeButton)
					.Text(LOCTEXT("MainMenu", "Main Menu"))
					.OnClicked_Lambda([this]() { if (AAmplitudeDirector* D = GetDirector()) { D->QuitToMainMenu(); } })
				]),
			0.6f)
	];

	InitialFocus = ResumeButton->GetFocusTarget();
}

FReply SAmplitudePauseMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// The pause keys (P, Start...) toggle back into the game.
	if (const UAmplitudeUserSettings* Settings = UAmplitudeUserSettings::Get())
	{
		if (Settings->GetActiveProfile().GetKeys(AmplitudeControls::Pause).Contains(InKeyEvent.GetKey()) && InKeyEvent.GetKey() != EKeys::Escape)
		{
			OnBack();
			return FReply::Handled();
		}
	}
	return SAmplitudeScreen::OnKeyDown(MyGeometry, InKeyEvent);
}

void SAmplitudePauseMenu::OnBack()
{
	if (AAmplitudeDirector* Owner = GetDirector())
	{
		Owner->ResumeGame();
	}
}

// ---------------------------------------------------------------------------------------------
// Results

void SAmplitudeResultsScreen::Construct(const FArguments& InArgs)
{
	Director = InArgs._Director;
	const AAmplitudeDirector* Owner = GetDirector();
	const FAmplitudeRunResult* Result = Owner != nullptr ? Owner->GetLastResult() : nullptr;
	if (Result == nullptr || !Result->Song.IsValid())
	{
		ChildSlot[SNullWidget::NullWidget];
		return;
	}

	const Amp::FRunSummary& Summary = Result->Summary;
	const bool bComplete = Summary.bCompleted;
	const int32 Judged = FMath::Max(1, Summary.Perfect + Summary.AutoHits + Summary.Good + Summary.Miss);
	auto Share = [Judged](int32 Count) { return FString::Printf(TEXT("%d (%.0f%%)"), Count, 100.0 * Count / Judged); };

	FString BestLane = TEXT("-");
	if (Summary.BestLane >= 0)
	{
		BestLane = FString::Printf(TEXT("%d  (%s)"), Summary.BestLaneCombo, *LaneText(Summary.BestLane).ToString());
	}
	FString WorstLane = TEXT("-");
	if (Summary.WorstLane >= 0)
	{
		WorstLane = FString::Printf(TEXT("%s  (%d mute(s), %d miss(es))"), *LaneText(Summary.WorstLane).ToString(), Summary.WorstLaneMutes, Summary.WorstLaneMisses);
	}

	FString RecordLine;
	if (Result->bNewRecord && Result->bHadPreviousBest)
	{
		RecordLine = FString::Printf(TEXT("Personal Best: %s (Previous) -> %s  NEW RECORD!"),
			*AmplitudeStyle::FormatScore(Result->PreviousBest).ToString(), *AmplitudeStyle::FormatScore(Summary.Score).ToString());
	}
	else if (Result->bNewRecord)
	{
		RecordLine = FString::Printf(TEXT("Personal Best: %s  NEW RECORD!"), *AmplitudeStyle::FormatScore(Summary.Score).ToString());
	}
	else if (Result->bHadPreviousBest)
	{
		RecordLine = FString::Printf(TEXT("Personal Best: %s"), *AmplitudeStyle::FormatScore(Result->PreviousBest).ToString());
	}
	if (Result->LeaderboardRank != INDEX_NONE)
	{
		RecordLine += FString::Printf(TEXT("    Leaderboard rank: #%d"), Result->LeaderboardRank + 1);
	}

	TSharedPtr<SAmplitudeButton> PlayAgainButton;
	TSharedRef<SVerticalBox> Stats = SNew(SVerticalBox);
	auto AddStat = [&Stats](const FText& Label, const FString& Value, const FLinearColor& Color)
	{
		Stats->AddSlot().AutoHeight().Padding(0.0f, 2.0f)[MakeStatLine(Label, FText::FromString(Value), Color)];
	};
	AddStat(LOCTEXT("Accuracy", "Accuracy:"), FString::Printf(TEXT("%.1f%%"), Summary.AccuracyPercent), AmplitudeStyle::Text);
	AddStat(LOCTEXT("PerfectHits", "Perfect Hits:"), Share(Summary.Perfect), AmplitudeStyle::Perfect);
	AddStat(LOCTEXT("GoodHits", "Good Hits:"), Share(Summary.Good), AmplitudeStyle::Good);
	AddStat(LOCTEXT("Misses", "Misses:"), Share(Summary.Miss), AmplitudeStyle::Miss);
	AddStat(LOCTEXT("AutoHits", "Auto-played:"), Share(Summary.AutoHits), AmplitudeStyle::TextDim);
	AddStat(LOCTEXT("Skipped", "Skipped (other lanes):"), FString::FromInt(Summary.Skipped), AmplitudeStyle::TextDim);
	AddStat(LOCTEXT("BestCombo", "Best Lane Combo:"), BestLane, AmplitudeStyle::Text);
	AddStat(LOCTEXT("WorstLane", "Worst Lane:"), WorstLane, AmplitudeStyle::Text);
	AddStat(LOCTEXT("Captures", "Lane Captures:"), FString::FromInt(Summary.Captures), AmplitudeStyle::Text);
	AddStat(LOCTEXT("Powerups", "Powerups Used:"), FString::FromInt(Summary.PowerupsCollected), AmplitudeStyle::Text);

	ChildSlot
	[
		AmplitudeUI::MakeBackdrop(
			AmplitudeUI::MakePanel(
				SNew(SBox)
				.WidthOverride(860.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					[
						AmplitudeUI::MakeTitle(bComplete ? LOCTEXT("Complete", "SONG COMPLETE!") : LOCTEXT("GameOver", "GAME OVER"), 56.0f,
							bComplete ? AmplitudeStyle::Perfect : AmplitudeStyle::Miss)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					[
						AmplitudeUI::MakeText(bComplete ? FText::GetEmpty() : LOCTEXT("EnergyZero", "(Energy = 0)"), 18.0f, AmplitudeStyle::TextDim)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 18.0f, 0.0f, 0.0f)
					[
						MakeStatLine(LOCTEXT("Song", "Song:"), FText::FromString(Result->Song->Title))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 2.0f)
					[
						MakeStatLine(LOCTEXT("DifficultyLabel", "Difficulty:"), DifficultyText(Result->Difficulty), AmplitudeStyle::GetDifficultyColor(Result->Difficulty))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 2.0f, 0.0f, 14.0f)
					[
						MakeStatLine(LOCTEXT("FinalScore", "Final Score:"), AmplitudeStyle::FormatScore(Summary.Score), AmplitudeStyle::Accent)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						AmplitudeUI::MakeSectionHeader(LOCTEXT("Statistics", "STATISTICS"))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						Stats
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 16.0f, 0.0f, 0.0f)
					[
						AmplitudeUI::MakeText(FText::FromString(RecordLine), 19.0f, Result->bNewRecord ? AmplitudeStyle::Perfect : AmplitudeStyle::Text, true)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(0.0f, 26.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(6.0f, 0.0f)
						[
							SAssignNew(PlayAgainButton, SAmplitudeButton)
							.Text(LOCTEXT("PlayAgain", "Play Again"))
							.MinWidth(180.0f)
							.FontSize(17.0f)
							.OnClicked_Lambda([this]() { if (AAmplitudeDirector* D = GetDirector()) { D->RestartSong(); } })
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(6.0f, 0.0f)
						[
							SNew(SAmplitudeButton)
							.Text(LOCTEXT("LeaderboardButton", "Leaderboard"))
							.MinWidth(180.0f)
							.FontSize(17.0f)
							.OnClicked_Lambda([this]() { if (AAmplitudeDirector* D = GetDirector()) { D->ShowLeaderboards(); } })
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(6.0f, 0.0f)
						[
							SNew(SAmplitudeButton)
							.Text(LOCTEXT("ResultsSongSelect", "Song Select"))
							.MinWidth(180.0f)
							.FontSize(17.0f)
							.OnClicked_Lambda([this]() { OnBack(); })
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(6.0f, 0.0f)
						[
							SNew(SAmplitudeButton)
							.Text(LOCTEXT("ResultsMainMenu", "Main Menu"))
							.MinWidth(180.0f)
							.FontSize(17.0f)
							.OnClicked_Lambda([this]() { if (AAmplitudeDirector* D = GetDirector()) { D->QuitToMainMenu(); } })
						]
					]
				]),
			0.72f)
	];

	InitialFocus = PlayAgainButton->GetFocusTarget();
}

void SAmplitudeResultsScreen::OnBack()
{
	if (AAmplitudeDirector* Owner = GetDirector())
	{
		Owner->QuitToSongSelect();
	}
}

// ---------------------------------------------------------------------------------------------
// Leaderboards

void SAmplitudeLeaderboardScreen::Construct(const FArguments& InArgs)
{
	Director = InArgs._Director;
	if (const AAmplitudeDirector* Owner = GetDirector())
	{
		SongIndex = Owner->GetSelectedSongIndex();
		Difficulty = Owner->GetSelectedDifficulty();
		if (const FAmplitudeRunResult* Result = Owner->GetLastResult())
		{
			const int32 ResultIndex = Result->Song.IsValid() ? Owner->GetSongLibrary().IndexOfId(Result->Song->Id) : INDEX_NONE;
			if (ResultIndex != INDEX_NONE)
			{
				SongIndex = ResultIndex;
				Difficulty = Result->Difficulty;
			}
		}
	}

	const float ColumnWidths[] = {90.0f, 200.0f, 300.0f, 140.0f, 200.0f};
	const FText Headers[] = {LOCTEXT("Rank", "RANK"), LOCTEXT("Score", "SCORE"), LOCTEXT("Player", "PLAYER"), LOCTEXT("AccuracyHeader", "ACCURACY"), LOCTEXT("Date", "DATE")};

	TSharedRef<SHorizontalBox> HeaderRow = SNew(SHorizontalBox);
	for (int32 Column = 0; Column < 5; ++Column)
	{
		HeaderRow->AddSlot()
		.AutoWidth()
		[
			SNew(SBox)
			.WidthOverride(ColumnWidths[Column])
			[
				AmplitudeUI::MakeText(Headers[Column], 16.0f, AmplitudeStyle::Accent, true)
			]
		];
	}

	TSharedRef<SVerticalBox> Table = SNew(SVerticalBox);
	Table->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[HeaderRow];
	for (int32 Row = 0; Row < FAmplitudeLeaderboard::MaxEntries; ++Row)
	{
		TSharedRef<SHorizontalBox> RowBox = SNew(SHorizontalBox);
		for (int32 Column = 0; Column < 5; ++Column)
		{
			RowBox->AddSlot()
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(ColumnWidths[Column])
				[
					SNew(STextBlock)
					.Text(this, &SAmplitudeLeaderboardScreen::GetCellText, Row, Column)
					.ColorAndOpacity(this, &SAmplitudeLeaderboardScreen::GetRowColor, Row)
					.Font(AmplitudeStyle::Font(18.0f))
				]
			];
		}
		Table->AddSlot().AutoHeight().Padding(0.0f, 3.0f)[RowBox];
	}

	TSharedPtr<SAmplitudeOptionRow> SongRow;
	ChildSlot
	[
		AmplitudeUI::MakeBackdrop(
			AmplitudeUI::MakePanel(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(0.0f, 0.0f, 0.0f, 18.0f)
				[
					AmplitudeUI::MakeTitle(LOCTEXT("LeaderboardsTitle", "LEADERBOARDS"), 48.0f)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 3.0f)
				[
					SAssignNew(SongRow, SAmplitudeOptionRow)
					.Label(LOCTEXT("SongRow", "SONG"))
					.LabelWidth(200.0f)
					.Value_Lambda([this]()
					{
						const AAmplitudeDirector* Owner = GetDirector();
						const TSharedPtr<const FAmplitudeSongDefinition> Song = Owner != nullptr ? Owner->GetSongLibrary().GetSong(SongIndex) : nullptr;
						return Song.IsValid() ? FText::FromString(Song->Title) : LOCTEXT("NoSongLoaded", "(no songs)");
					})
					.OnDecrement_Lambda([this]() { CycleSong(-1); })
					.OnIncrement_Lambda([this]() { CycleSong(1); })
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 3.0f, 0.0f, 18.0f)
				[
					SNew(SAmplitudeOptionRow)
					.Label(LOCTEXT("DifficultyRow", "DIFFICULTY"))
					.LabelWidth(200.0f)
					.Value_Lambda([this]() { return DifficultyText(Difficulty); })
					.OnDecrement_Lambda([this]() { CycleDifficulty(-1); })
					.OnIncrement_Lambda([this]() { CycleDifficulty(1); })
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					Table
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(0.0f, 24.0f, 0.0f, 0.0f)
				[
					SNew(SAmplitudeButton)
					.Text(LOCTEXT("BackButton", "Back"))
					.MinWidth(220.0f)
					.OnClicked_Lambda([this]() { OnBack(); })
				]),
			0.65f)
	];

	InitialFocus = SongRow;
}

void SAmplitudeLeaderboardScreen::CycleSong(int32 Direction)
{
	const AAmplitudeDirector* Owner = GetDirector();
	const int32 Count = Owner != nullptr ? Owner->GetSongLibrary().Num() : 0;
	if (Count > 0)
	{
		SongIndex = (SongIndex + Direction + Count) % Count;
	}
}

void SAmplitudeLeaderboardScreen::CycleDifficulty(int32 Direction)
{
	const int32 Index = (static_cast<int32>(Difficulty) + Direction + Amp::NumDifficulties) % Amp::NumDifficulties;
	Difficulty = static_cast<Amp::EDifficulty>(Index);
}

FText SAmplitudeLeaderboardScreen::GetCellText(int32 Row, int32 Column) const
{
	const AAmplitudeDirector* Owner = GetDirector();
	const TSharedPtr<const FAmplitudeSongDefinition> Song = Owner != nullptr ? Owner->GetSongLibrary().GetSong(SongIndex) : nullptr;
	if (!Song.IsValid())
	{
		return FText::GetEmpty();
	}
	const TArray<FAmplitudeScoreEntry>& Entries = Owner->GetLeaderboard().GetEntries(Song->Id, Difficulty);
	if (!Entries.IsValidIndex(Row))
	{
		return Column == 0 ? FText::FromString(FString::Printf(TEXT("%2d."), Row + 1)) : (Column == 1 ? FText::FromString(TEXT("-")) : FText::GetEmpty());
	}

	const FAmplitudeScoreEntry& Entry = Entries[Row];
	switch (Column)
	{
	case 0:
		return FText::FromString(FString::Printf(TEXT("%2d."), Row + 1));
	case 1:
		return AmplitudeStyle::FormatScore(Entry.Score);
	case 2:
		return FText::FromString(Entry.bCompleted ? Entry.PlayerName : Entry.PlayerName + TEXT(" (KO)"));
	case 3:
		return Percent(Entry.AccuracyPercent);
	default:
		return FText::AsDate(Entry.Date, EDateTimeStyle::Short);
	}
}

FSlateColor SAmplitudeLeaderboardScreen::GetRowColor(int32 Row) const
{
	// Highlight the score that was just set.
	const AAmplitudeDirector* Owner = GetDirector();
	const FAmplitudeRunResult* Result = Owner != nullptr ? Owner->GetLastResult() : nullptr;
	const TSharedPtr<const FAmplitudeSongDefinition> Song = Owner != nullptr ? Owner->GetSongLibrary().GetSong(SongIndex) : nullptr;
	if (Result != nullptr && Song.IsValid() && Result->Song.IsValid() && Result->Song->Id == Song->Id && Result->Difficulty == Difficulty && Result->LeaderboardRank == Row)
	{
		return FSlateColor(AmplitudeStyle::Perfect);
	}
	return FSlateColor(Row == 0 ? AmplitudeStyle::Text : AmplitudeStyle::TextDim);
}

void SAmplitudeLeaderboardScreen::OnBack()
{
	if (AAmplitudeDirector* Owner = GetDirector())
	{
		Owner->CloseLeaderboards();
	}
}

// ---------------------------------------------------------------------------------------------
// Credits

void SAmplitudeCreditsScreen::Construct(const FArguments& InArgs)
{
	Director = InArgs._Director;

	TSharedPtr<SAmplitudeButton> BackButton;
	ChildSlot
	[
		AmplitudeUI::MakeBackdrop(
			AmplitudeUI::MakePanel(
				SNew(SBox)
				.WidthOverride(820.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(0.0f, 0.0f, 0.0f, 20.0f)
					[
						AmplitudeUI::MakeTitle(LOCTEXT("CreditsTitle", "CREDITS"), 48.0f)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						AmplitudeUI::MakeText(FText::Format(LOCTEXT("CreditsBody",
							"AMPLITUDE - a lane-based rhythm-action game\n\n"
							"Game design: Amplitude UE5 specification v1.0\n"
							"Engine: Unreal Engine {0}\n"
							"Gameplay core, 3D stage, audio engine and UI written in C++ (no content assets required)\n"
							"Sound effects: synthesised in real time\n"
							"Demo song \"Neon Drive\": procedurally generated by Tools/generate_demo_song.py\n\n"
							"Thanks for playing!"), FText::FromString(FEngineVersion::Current().ToString(EVersionComponent::Minor))), 19.0f)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(0.0f, 28.0f, 0.0f, 0.0f)
					[
						SAssignNew(BackButton, SAmplitudeButton)
						.Text(LOCTEXT("CreditsBack", "Back"))
						.MinWidth(220.0f)
						.OnClicked_Lambda([this]() { OnBack(); })
					]
				]),
			0.6f)
	];

	InitialFocus = BackButton->GetFocusTarget();
}

void SAmplitudeCreditsScreen::OnBack()
{
	if (AAmplitudeDirector* Owner = GetDirector())
	{
		Owner->ShowMainMenu();
	}
}

// ---------------------------------------------------------------------------------------------
// Loading

void SAmplitudeLoadingScreen::Construct(const FArguments& InArgs)
{
	Director = InArgs._Director;
	const AAmplitudeDirector* Owner = GetDirector();
	const TSharedPtr<const FAmplitudeSongDefinition> Song = Owner != nullptr ? Owner->GetSelectedSong() : nullptr;
	const FText SongTitle = Song.IsValid() ? FText::FromString(Song->Title) : FText::GetEmpty();
	const bool bFailed = Owner != nullptr && !Owner->IsLoading() && !Owner->GetLoadError().IsEmpty();

	TSharedPtr<SAmplitudeButton> FirstButton;
	TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox);
	if (bFailed)
	{
		Buttons->AddSlot()
		.AutoWidth()
		.Padding(10.0f, 0.0f)
		[
			SAssignNew(FirstButton, SAmplitudeButton)
			.Text(LOCTEXT("PlaySilently", "Play Without Audio"))
			.MinWidth(260.0f)
			.OnClicked_Lambda([this]() { if (AAmplitudeDirector* D = GetDirector()) { D->StartSelectedSongWithoutAudio(); } })
		];
	}
	TSharedPtr<SAmplitudeButton> BackButton;
	Buttons->AddSlot()
	.AutoWidth()
	.Padding(10.0f, 0.0f)
	[
		SAssignNew(BackButton, SAmplitudeButton)
		.Text(bFailed ? LOCTEXT("LoadingBack", "Back") : LOCTEXT("LoadingCancel", "Cancel"))
		.MinWidth(220.0f)
		.OnClicked_Lambda([this]() { OnBack(); })
	];

	ChildSlot
	[
		AmplitudeUI::MakeBackdrop(
			AmplitudeUI::MakePanel(
				SNew(SBox)
				.WidthOverride(820.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					[
						AmplitudeUI::MakeTitle(bFailed ? LOCTEXT("LoadFailed", "COULD NOT LOAD AUDIO") : LOCTEXT("Loading", "LOADING"), 44.0f,
							bFailed ? AmplitudeStyle::Miss : AmplitudeStyle::Accent)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(0.0f, 12.0f)
					[
						AmplitudeUI::MakeText(SongTitle, 26.0f, AmplitudeStyle::Text, true)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f)
					[
						AmplitudeUI::MakeText(bFailed ? FText::FromString(Owner->GetLoadError()) : LOCTEXT("Decoding", "Decoding multitrack audio..."), 17.0f, AmplitudeStyle::TextDim)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(0.0f, 24.0f, 0.0f, 0.0f)
					[
						Buttons
					]
				]),
			0.75f)
	];

	InitialFocus = FirstButton.IsValid() ? FirstButton->GetFocusTarget() : BackButton->GetFocusTarget();
}

void SAmplitudeLoadingScreen::OnBack()
{
	if (AAmplitudeDirector* Owner = GetDirector())
	{
		Owner->QuitToSongSelect();
	}
}

#undef LOCTEXT_NAMESPACE
