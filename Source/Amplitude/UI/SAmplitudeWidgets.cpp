#include "UI/SAmplitudeWidgets.h"

#include "Framework/Application/SlateApplication.h"
#include "Game/AmplitudeDirector.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	constexpr int32 BarSegments = 10;

	FLinearColor WithAlpha(const FLinearColor& Color, float Alpha)
	{
		FLinearColor Result = Color;
		Result.A = Alpha;
		return Result;
	}
}

// ---------------------------------------------------------------------------------------------
// SAmplitudeScreen

AAmplitudeDirector* SAmplitudeScreen::GetDirector() const
{
	return Director.Get();
}

FReply SAmplitudeScreen::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (AmplitudeUI::IsBackKey(InKeyEvent))
	{
		AmplitudeUI::PlayUiSfx(Amp::ESfx::UiBack);
		OnBack();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

// ---------------------------------------------------------------------------------------------
// SAmplitudeButton

void SAmplitudeButton::Construct(const FArguments& InArgs)
{
	IsSelected = InArgs._IsSelected;
	AccentColor = InArgs._AccentColor;
	OnClickedDelegate = InArgs._OnClicked;
	OnFocusedDelegate = InArgs._OnFocused;

	ChildSlot
	[
		SNew(SBox)
		.MinDesiredWidth(InArgs._MinWidth)
		[
			SAssignNew(Button, SButton)
			.ButtonStyle(&AmplitudeStyle::ButtonStyle())
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ButtonColorAndOpacity(this, &SAmplitudeButton::GetButtonTint)
			.OnClicked_Lambda([this]()
			{
				AmplitudeUI::PlayUiSfx(Amp::ESfx::UiConfirm);
				OnClickedDelegate.ExecuteIfBound();
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(InArgs._Text)
				.Font(AmplitudeStyle::Font(InArgs._FontSize))
				.ColorAndOpacity(this, &SAmplitudeButton::GetTextColor)
				.Justification(ETextJustify::Center)
			]
		]
	];
}

void SAmplitudeButton::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	const bool bFocused = Button.IsValid() && Button->HasAnyUserFocus().IsSet();
	if (bFocused && !bWasFocused)
	{
		AmplitudeUI::PlayUiSfx(Amp::ESfx::UiMove);
		OnFocusedDelegate.ExecuteIfBound();
	}
	bWasFocused = bFocused;
}

TSharedRef<SWidget> SAmplitudeButton::GetFocusTarget() const
{
	return Button.ToSharedRef();
}

bool SAmplitudeButton::IsFocusedOrHovered() const
{
	return Button.IsValid() && (Button->HasAnyUserFocus().IsSet() || Button->IsHovered());
}

FSlateColor SAmplitudeButton::GetButtonTint() const
{
	const bool bSelected = IsSelected.Get(false);
	if (IsFocusedOrHovered())
	{
		return FSlateColor(WithAlpha(AccentColor, bSelected ? 3.0f : 2.5f));
	}
	return FSlateColor(bSelected ? WithAlpha(AccentColor, 1.4f) : FLinearColor::White);
}

FSlateColor SAmplitudeButton::GetTextColor() const
{
	if (IsFocusedOrHovered())
	{
		return FSlateColor(FLinearColor::White);
	}
	return FSlateColor(IsSelected.Get(false) ? AccentColor : AmplitudeStyle::Text);
}

// ---------------------------------------------------------------------------------------------
// SAmplitudeOptionRow

void SAmplitudeOptionRow::Construct(const FArguments& InArgs)
{
	BarFraction = InArgs._BarFraction;
	OnDecrement = InArgs._OnDecrement;
	OnIncrement = InArgs._OnIncrement;
	OnActivate = InArgs._OnActivate;

	TSharedRef<SHorizontalBox> Bar = SNew(SHorizontalBox)
		.Visibility_Lambda([this]() { return BarFraction.Get().IsSet() ? EVisibility::Visible : EVisibility::Collapsed; });
	for (int32 Segment = 0; Segment < BarSegments; ++Segment)
	{
		Bar->AddSlot()
		.AutoWidth()
		.Padding(1.5f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(14.0f)
			.HeightOverride(18.0f)
			[
				SNew(SImage)
				.Image(AmplitudeStyle::WhiteBrush())
				.ColorAndOpacity_Lambda([this, Segment]()
				{
					const float Fraction = BarFraction.Get().Get(0.0f);
					const bool bFilled = Segment < FMath::RoundToInt32(Fraction * BarSegments);
					return FSlateColor(bFilled ? (IsHighlighted() ? AmplitudeStyle::Accent : AmplitudeStyle::Text) : WithAlpha(AmplitudeStyle::TextDim, 0.25f));
				})
			]
		];
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(AmplitudeStyle::RoundedBrush())
		.BorderBackgroundColor_Lambda([this]()
		{
			return FSlateColor(IsHighlighted() ? WithAlpha(AmplitudeStyle::Accent, 0.18f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.04f));
		})
		.Padding(FMargin(18.0f, 8.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(InArgs._LabelWidth)
				[
					SNew(STextBlock)
					.Text(InArgs._Label)
					.Font(AmplitudeStyle::Font(17.0f))
					.ColorAndOpacity_Lambda([this]() { return FSlateColor(IsHighlighted() ? FLinearColor::White : AmplitudeStyle::TextDim); })
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				MakeArrowButton(TEXT("<"), false)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 12.0f, 0.0f)
				[
					Bar
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(InArgs._Value)
					.Font(AmplitudeStyle::Font(18.0f))
					.ColorAndOpacity(FSlateColor(AmplitudeStyle::Text))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				MakeArrowButton(TEXT(">"), true)
			]
		]
	];
}

TSharedRef<SWidget> SAmplitudeOptionRow::MakeArrowButton(const TCHAR* Glyph, bool bIncrement)
{
	return SNew(SButton)
		.ButtonStyle(&AmplitudeStyle::ArrowButtonStyle())
		.IsFocusable(false)
		.ContentPadding(FMargin(12.0f, 2.0f))
		.OnClicked_Lambda([this, bIncrement]()
		{
			if (bIncrement)
			{
				Increment();
			}
			else
			{
				Decrement();
			}
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
			.Text(FText::FromString(Glyph))
			.Font(AmplitudeStyle::Font(20.0f))
			.ColorAndOpacity_Lambda([this]() { return FSlateColor(IsHighlighted() ? AmplitudeStyle::Accent : AmplitudeStyle::TextDim); })
		];
}

bool SAmplitudeOptionRow::IsHighlighted() const
{
	return HasAnyUserFocus().IsSet() || IsHovered();
}

void SAmplitudeOptionRow::Decrement()
{
	AmplitudeUI::PlayUiSfx(Amp::ESfx::UiMove);
	OnDecrement.ExecuteIfBound();
}

void SAmplitudeOptionRow::Increment()
{
	AmplitudeUI::PlayUiSfx(Amp::ESfx::UiMove);
	OnIncrement.ExecuteIfBound();
}

void SAmplitudeOptionRow::Activate()
{
	if (OnActivate.IsBound())
	{
		AmplitudeUI::PlayUiSfx(Amp::ESfx::UiConfirm);
		OnActivate.Execute();
	}
	else
	{
		Increment();
	}
}

FReply SAmplitudeOptionRow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const EUINavigation Direction = FSlateApplication::Get().GetNavigationDirectionFromKey(InKeyEvent);
	if (Direction == EUINavigation::Left)
	{
		Decrement();
		return FReply::Handled();
	}
	if (Direction == EUINavigation::Right)
	{
		Increment();
		return FReply::Handled();
	}
	if (AmplitudeUI::IsAcceptKey(InKeyEvent))
	{
		Activate();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

FReply SAmplitudeOptionRow::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	// The < and > buttons handle their own clicks; clicking the rest of the row activates it.
	Activate();
	return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}

FReply SAmplitudeOptionRow::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	AmplitudeUI::PlayUiSfx(Amp::ESfx::UiMove);
	return FReply::Handled();
}

// ---------------------------------------------------------------------------------------------
// SAmplitudeKeyBinder

void SAmplitudeKeyBinder::Construct(const FArguments& InArgs)
{
	Key = InArgs._Key;
	OnKeyChosen = InArgs._OnKeyChosen;
	bGamepad = InArgs._bGamepad;

	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(InArgs._Width)
		.HeightOverride(40.0f)
		[
			SNew(SBorder)
			.BorderImage(AmplitudeStyle::RoundedBrush())
			.BorderBackgroundColor(this, &SAmplitudeKeyBinder::GetBackgroundColor)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SAmplitudeKeyBinder::GetDisplayText)
				.Font(AmplitudeStyle::Font(14.0f))
				.ColorAndOpacity_Lambda([this]()
				{
					return FSlateColor(bListening ? AmplitudeStyle::Perfect : (HasAnyUserFocus().IsSet() ? FLinearColor::White : AmplitudeStyle::Text));
				})
			]
		]
	];
}

FText SAmplitudeKeyBinder::GetDisplayText() const
{
	if (bListening)
	{
		return FText::FromString(bGamepad ? TEXT("PRESS BUTTON") : TEXT("PRESS KEY"));
	}
	return AmplitudeStyle::GetKeyLabel(Key.Get(EKeys::Invalid));
}

FSlateColor SAmplitudeKeyBinder::GetBackgroundColor() const
{
	if (bListening)
	{
		return FSlateColor(WithAlpha(AmplitudeStyle::Perfect, 0.3f));
	}
	if (HasAnyUserFocus().IsSet() || IsHovered())
	{
		return FSlateColor(WithAlpha(AmplitudeStyle::Accent, 0.3f));
	}
	return FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.06f));
}

FReply SAmplitudeKeyBinder::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Pressed = InKeyEvent.GetKey();
	if (bListening)
	{
		if (Pressed == EKeys::Escape)
		{
			bListening = false;
			AmplitudeUI::PlayUiSfx(Amp::ESfx::UiBack);
		}
		else if (Pressed.IsGamepadKey() == bGamepad)
		{
			bListening = false;
			AmplitudeUI::PlayUiSfx(Amp::ESfx::UiConfirm);
			OnKeyChosen.ExecuteIfBound(Pressed);
		}
		// While listening every key is swallowed so it cannot navigate away.
		return FReply::Handled();
	}

	if (Pressed == EKeys::Delete)
	{
		AmplitudeUI::PlayUiSfx(Amp::ESfx::UiBack);
		OnKeyChosen.ExecuteIfBound(EKeys::Invalid);
		return FReply::Handled();
	}
	if (AmplitudeUI::IsAcceptKey(InKeyEvent))
	{
		bListening = true;
		AmplitudeUI::PlayUiSfx(Amp::ESfx::UiConfirm);
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

FReply SAmplitudeKeyBinder::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	bListening = true;
	AmplitudeUI::PlayUiSfx(Amp::ESfx::UiConfirm);
	return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}

FReply SAmplitudeKeyBinder::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	AmplitudeUI::PlayUiSfx(Amp::ESfx::UiMove);
	return FReply::Handled();
}

void SAmplitudeKeyBinder::OnFocusLost(const FFocusEvent& InFocusEvent)
{
	bListening = false;
	SCompoundWidget::OnFocusLost(InFocusEvent);
}

// ---------------------------------------------------------------------------------------------
// Helpers

namespace AmplitudeUI
{
	bool IsBackKey(const FKeyEvent& KeyEvent)
	{
		const FKey Key = KeyEvent.GetKey();
		if (Key == EKeys::Escape || Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right)
		{
			return true;
		}
		return FSlateApplication::IsInitialized() && FSlateApplication::Get().GetNavigationActionFromKey(KeyEvent) == EUINavigationAction::Back;
	}

	bool IsAcceptKey(const FKeyEvent& KeyEvent)
	{
		const FKey Key = KeyEvent.GetKey();
		if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom)
		{
			return true;
		}
		return FSlateApplication::IsInitialized() && FSlateApplication::Get().GetNavigationActionFromKey(KeyEvent) == EUINavigationAction::Accept;
	}

	TSharedRef<SWidget> MakeTitle(const FText& Title, float Size, const FLinearColor& Color)
	{
		return SNew(STextBlock)
			.Text(Title)
			.Font(AmplitudeStyle::Font(Size))
			.ColorAndOpacity(FSlateColor(Color))
			.ShadowOffset(FVector2D(0.0f, 0.0f))
			.Justification(ETextJustify::Center);
	}

	TSharedRef<SWidget> MakeText(const TAttribute<FText>& Text, float Size, const FLinearColor& Color, bool bBold)
	{
		return SNew(STextBlock)
			.Text(Text)
			.Font(AmplitudeStyle::Font(Size, bBold))
			.ColorAndOpacity(FSlateColor(Color))
			.AutoWrapText(true);
	}

	TSharedRef<SWidget> MakeSectionHeader(const FText& Text)
	{
		return SNew(SBox)
			.Padding(FMargin(4.0f, 18.0f, 4.0f, 6.0f))
			[
				SNew(STextBlock)
				.Text(Text)
				.Font(AmplitudeStyle::Font(20.0f))
				.ColorAndOpacity(FSlateColor(AmplitudeStyle::Accent))
			];
	}

	TSharedRef<SWidget> MakePanel(const TSharedRef<SWidget>& Content, const FMargin& Padding)
	{
		return SNew(SBorder)
			.BorderImage(AmplitudeStyle::PanelBrush())
			.Padding(Padding)
			[
				Content
			];
	}

	TSharedRef<SWidget> MakeBackdrop(const TSharedRef<SWidget>& Content, float Opacity)
	{
		return SNew(SBorder)
			.BorderImage(AmplitudeStyle::WhiteBrush())
			.BorderBackgroundColor(FSlateColor(FLinearColor(0.0f, 0.0f, 0.0f, Opacity)))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(FMargin(24.0f))
			[
				Content
			];
	}
}
