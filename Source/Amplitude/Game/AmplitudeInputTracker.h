#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"
#include "InputCoreTypes.h"

/**
 * Slate input pre-processor. It never consumes input; it only records:
 * - which device was used last (keyboard/mouse vs gamepad) for on-screen button prompts, and
 * - when each key went down, so lane presses are timestamped when Slate received them rather
 *   than later in the frame when Enhanced Input fires its action (lower judged latency).
 */
class FAmplitudeInputTracker : public IInputProcessor
{
public:
	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override;
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override;
	virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;

	bool IsUsingGamepad() const { return bUsingGamepad; }

	/** Most recent key-down time among Keys within MaxAgeSeconds of Now; Now if there is none. */
	double FindRecentPress(const TArray<FKey>& Keys, double Now, double MaxAgeSeconds) const;

private:
	TMap<FKey, double> LastKeyDownSeconds;
	bool bUsingGamepad = false;
};
