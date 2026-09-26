#include "Game/AmplitudeInputTracker.h"

#include "HAL/PlatformTime.h"
#include "Input/Events.h"

namespace
{
	constexpr float AnalogDeadZone = 0.35f;
	constexpr double MouseMoveThresholdSquared = 16.0;
}

void FAmplitudeInputTracker::Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
}

bool FAmplitudeInputTracker::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	bUsingGamepad = Key.IsGamepadKey();
	if (!InKeyEvent.IsRepeat())
	{
		LastKeyDownSeconds.Add(Key, FPlatformTime::Seconds());
	}
	return false;
}

bool FAmplitudeInputTracker::HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent)
{
	if (FMath::Abs(InAnalogInputEvent.GetAnalogValue()) > AnalogDeadZone)
	{
		bUsingGamepad = true;
	}
	return false;
}

bool FAmplitudeInputTracker::HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	const FVector2D Delta(MouseEvent.GetCursorDelta());
	if (Delta.SizeSquared() > MouseMoveThresholdSquared)
	{
		bUsingGamepad = false;
	}
	return false;
}

bool FAmplitudeInputTracker::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	bUsingGamepad = false;
	return false;
}

double FAmplitudeInputTracker::FindRecentPress(const TArray<FKey>& Keys, double Now, double MaxAgeSeconds) const
{
	double Best = -1.0;
	for (const FKey& Key : Keys)
	{
		const double* Seconds = LastKeyDownSeconds.Find(Key);
		if (Seconds != nullptr && *Seconds <= Now && Now - *Seconds <= MaxAgeSeconds && *Seconds > Best)
		{
			Best = *Seconds;
		}
	}
	return Best >= 0.0 ? Best : Now;
}
