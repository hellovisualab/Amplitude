#include "Audio/AmplitudeStemPlayerComponent.h"

#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"

namespace
{
	constexpr double MuteCrossfadeMs = 200.0;
	constexpr double MusicGainRampMs = 50.0;
}

UAmplitudeStemPlayerComponent::UAmplitudeStemPlayerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NumChannels = 2;
	bAutoActivate = false;
	bAllowSpatialization = false;
	bIsUISound = true;
}

void UAmplitudeStemPlayerComponent::LoadSong(std::shared_ptr<const Amp::FSongAudio> Song, double StartMs)
{
	{
		FScopeLock Lock(&PositionLock);
		Position = FAmplitudeAudioPosition();
		Position.PositionMs = StartMs;
		Position.Serial = NextSerial++;
	}

	SynthCommand([this, Song = MoveTemp(Song), StartMs]() mutable
	{
		Mixer.SetSong(MoveTemp(Song));
		Mixer.Seek(StartMs);
		Mixer.SetRate(1.0);
		Mixer.SetSoloLane(-1);
		Mixer.SetPlaying(false);
	});
}

void UAmplitudeStemPlayerComponent::Unload()
{
	SynthCommand([this]()
	{
		Mixer.SetPlaying(false);
		Mixer.SetSong(nullptr);
	});
}

void UAmplitudeStemPlayerComponent::SetPlaying(bool bPlaying)
{
	SynthCommand([this, bPlaying]() { Mixer.SetPlaying(bPlaying); });
}

void UAmplitudeStemPlayerComponent::SetPlaybackRate(double Rate)
{
	SynthCommand([this, Rate]() { Mixer.SetRate(Rate); });
}

void UAmplitudeStemPlayerComponent::SetLaneMuted(int32 Lane, bool bMuted)
{
	SynthCommand([this, Lane, bMuted]() { Mixer.SetLaneGain(Lane, bMuted ? 0.0f : 1.0f, MuteCrossfadeMs); });
}

void UAmplitudeStemPlayerComponent::SetMusicGain(float LinearGain)
{
	SynthCommand([this, LinearGain]() { Mixer.SetMasterGain(LinearGain, MusicGainRampMs); });
}

void UAmplitudeStemPlayerComponent::SetSoloLane(int32 Lane)
{
	SynthCommand([this, Lane]() { Mixer.SetSoloLane(Lane); });
}

FAmplitudeAudioPosition UAmplitudeStemPlayerComponent::GetAudioPosition() const
{
	FScopeLock Lock(&PositionLock);
	return Position;
}

bool UAmplitudeStemPlayerComponent::Init(int32& SampleRate)
{
	NumChannels = 2;
	const int32 Rate = SampleRate;
	const int32 Channels = NumChannels;
	SynthCommand([this, Rate, Channels]() { Mixer.SetOutputFormat(Rate, Channels); });
	return true;
}

int32 UAmplitudeStemPlayerComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	const int32 Channels = FMath::Max(1, Mixer.GetOutputChannels());
	const int32 NumFrames = NumSamples / Channels;
	const double StartMs = Mixer.GetPositionMs();

	Mixer.Render(OutAudio, NumFrames);
	for (int32 Index = NumFrames * Channels; Index < NumSamples; ++Index)
	{
		OutAudio[Index] = 0.0f;
	}

	FAmplitudeAudioPosition Snapshot;
	Snapshot.PositionMs = StartMs;
	Snapshot.RenderedAtSeconds = FPlatformTime::Seconds();
	Snapshot.BufferMs = static_cast<double>(NumFrames) * 1000.0 / static_cast<double>(FMath::Max(1, Mixer.GetOutputSampleRate()));
	Snapshot.Rate = Mixer.GetRate();
	Snapshot.bPlaying = Mixer.IsPlaying();
	{
		FScopeLock Lock(&PositionLock);
		Snapshot.Serial = NextSerial++;
		Position = Snapshot;
	}
	return NumSamples;
}
