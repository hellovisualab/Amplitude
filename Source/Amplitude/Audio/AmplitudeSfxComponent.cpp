#include "Audio/AmplitudeSfxComponent.h"

UAmplitudeSfxComponent::UAmplitudeSfxComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NumChannels = 2;
	bAutoActivate = false;
	bAllowSpatialization = false;
	bIsUISound = true;
}

void UAmplitudeSfxComponent::Play(Amp::ESfx Sfx)
{
	SynthCommand([this, Sfx]() { Synth.Trigger(Sfx); });
}

void UAmplitudeSfxComponent::StopAllSounds()
{
	SynthCommand([this]() { Synth.StopAll(); });
}

void UAmplitudeSfxComponent::SetSfxGain(float LinearGain)
{
	SynthCommand([this, LinearGain]() { Synth.SetMasterGain(LinearGain); });
}

bool UAmplitudeSfxComponent::Init(int32& SampleRate)
{
	NumChannels = 2;
	const int32 Rate = SampleRate;
	const int32 Channels = NumChannels;
	SynthCommand([this, Rate, Channels]() { Synth.SetOutputFormat(Rate, Channels); });
	return true;
}

int32 UAmplitudeSfxComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	const int32 Channels = FMath::Max(1, NumChannels);
	const int32 NumFrames = NumSamples / Channels;
	Synth.Render(OutAudio, NumFrames);
	for (int32 Index = NumFrames * Channels; Index < NumSamples; ++Index)
	{
		OutAudio[Index] = 0.0f;
	}
	return NumSamples;
}
