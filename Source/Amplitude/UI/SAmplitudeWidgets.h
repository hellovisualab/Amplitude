#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "UI/AmplitudeStyle.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class AAmplitudeDirector;
class SButton;

/** Base class for menu screens: knows its director, which widget takes focus, and handles Back. */
class SAmplitudeScreen : public SCompoundWidget
{
public:
	virtual TSharedPtr<SWidget> GetInitialFocus() const { return InitialFocus; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

protected:
	/** Escape, Backspace or gamepad B/Circle (spec 18.2). */
	virtual void OnBack() {}

	AAmplitudeDirector* GetDirector() const;

	TWeakObjectPtr<AAmplitudeDirector> Director;
	TSharedPtr<SWidget> InitialFocus;
};

/** Menu button with a neon focus/hover highlight that works for mouse, keyboard and gamepad. */
class SAmplitudeButton : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAmplitudeButton)
		: _IsSelected(false)
		, _MinWidth(300.0f)
		, _FontSize(20.0f)
		, _AccentColor(AmplitudeStyle::Accent)
	{}
		SLATE_ATTRIBUTE(FText, Text)
		/** Persistent highlight, e.g. the chosen difficulty. */
		SLATE_ATTRIBUTE(bool, IsSelected)
		SLATE_ARGUMENT(float, MinWidth)
		SLATE_ARGUMENT(float, FontSize)
		SLATE_ARGUMENT(FLinearColor, AccentColor)
		SLATE_EVENT(FSimpleDelegate, OnClicked)
		SLATE_EVENT(FSimpleDelegate, OnFocused)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** The inner focusable button. */
	TSharedRef<SWidget> GetFocusTarget() const;

private:
	bool IsFocusedOrHovered() const;
	FSlateColor GetButtonTint() const;
	FSlateColor GetTextColor() const;

	TSharedPtr<SButton> Button;
	TAttribute<bool> IsSelected;
	FLinearColor AccentColor;
	FSimpleDelegate OnClickedDelegate;
	FSimpleDelegate OnFocusedDelegate;
	bool bWasFocused = false;
};

/**
 * One settings row: "LABEL   <  value  >". Left/Right (keys, D-pad or clicking the arrows) change the
 * value; Accept activates it. With BarFraction set it draws a [#####-----] bar (spec 11.4 volume sliders).
 */
class SAmplitudeOptionRow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAmplitudeOptionRow)
		: _LabelWidth(300.0f)
	{}
		SLATE_ATTRIBUTE(FText, Label)
		SLATE_ATTRIBUTE(FText, Value)
		SLATE_ATTRIBUTE(TOptional<float>, BarFraction)
		SLATE_ARGUMENT(float, LabelWidth)
		SLATE_EVENT(FSimpleDelegate, OnDecrement)
		SLATE_EVENT(FSimpleDelegate, OnIncrement)
		/** Defaults to OnIncrement when unbound (so Enter toggles ON/OFF rows). */
		SLATE_EVENT(FSimpleDelegate, OnActivate)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;

private:
	TSharedRef<SWidget> MakeArrowButton(const TCHAR* Glyph, bool bIncrement);
	bool IsHighlighted() const;
	void Decrement();
	void Increment();
	void Activate();

	TAttribute<TOptional<float>> BarFraction;
	FSimpleDelegate OnDecrement;
	FSimpleDelegate OnIncrement;
	FSimpleDelegate OnActivate;
};

DECLARE_DELEGATE_OneParam(FOnAmplitudeKeyChosen, FKey);

/**
 * Key rebinding cell: Accept/click starts listening, the next key (or gamepad button) is chosen,
 * Escape cancels and Delete clears the slot.
 */
class SAmplitudeKeyBinder : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAmplitudeKeyBinder)
		: _bGamepad(false)
		, _Width(170.0f)
	{}
		SLATE_ATTRIBUTE(FKey, Key)
		/** Gamepad slots accept only gamepad buttons, keyboard slots only keys. */
		SLATE_ARGUMENT(bool, bGamepad)
		SLATE_ARGUMENT(float, Width)
		SLATE_EVENT(FOnAmplitudeKeyChosen, OnKeyChosen)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
	virtual void OnFocusLost(const FFocusEvent& InFocusEvent) override;

private:
	FText GetDisplayText() const;
	FSlateColor GetBackgroundColor() const;

	TAttribute<FKey> Key;
	FOnAmplitudeKeyChosen OnKeyChosen;
	bool bGamepad = false;
	bool bListening = false;
};

namespace AmplitudeUI
{
	bool IsBackKey(const FKeyEvent& KeyEvent);
	bool IsAcceptKey(const FKeyEvent& KeyEvent);

	TSharedRef<SWidget> MakeTitle(const FText& Title, float Size = 56.0f, const FLinearColor& Color = AmplitudeStyle::Accent);
	TSharedRef<SWidget> MakeText(const TAttribute<FText>& Text, float Size = 18.0f, const FLinearColor& Color = AmplitudeStyle::Text, bool bBold = false);
	TSharedRef<SWidget> MakeSectionHeader(const FText& Text);
	/** Rounded translucent panel around Content. */
	TSharedRef<SWidget> MakePanel(const TSharedRef<SWidget>& Content, const FMargin& Padding = FMargin(48.0f, 32.0f));
	/** Full-screen dimmed backdrop with Content centred on it. */
	TSharedRef<SWidget> MakeBackdrop(const TSharedRef<SWidget>& Content, float Opacity = 0.7f);
}
