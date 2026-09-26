#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "Core/AmpStemMixer.h"

#include <memory>

#include "AmplitudeStemPlayerComponent.generated.h"

/** Play head snapshot published by the audio render thread after each buffer. */
struct FAmplitudeAudioPosition
{
	/** Song position (ms) of the first frame of the last rendered buffer. */
	double PositionMs = 0.0;
	/** FPlatformTime::Seconds() when that buffer was rendered. */
	double RenderedAtSeconds = 0.0;
	/** Duration of the buffer, used as the output latency estimate. */
	double BufferMs = 0.0;
	double Rate = 1.0;
	uint64 Serial = 0;
	bool bPlaying = false;
};

/**
 * Plays a song's six instrument tracks (spec 10.1) through the Unreal audio mixer.
 *
 * All mixing (per-lane mute crossfades, Slow Motion resampling, solo) happens in Amp::FStemMixer on
 * the audio render thread. Game-thread calls are forwarded as synth commands, and the play head is
 * published back so the song clock can be slaved to the audio (spec 13.3).
 */
UCLASS(ClassGroup = Amplitude)
class AMPLITUDE_API UAmplitudeStemPlayerComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	UAmplitudeStemPlayerComponent(const FObjectInitializer& ObjectInitializer);

	/** Replaces the loaded song and parks the play head at StartMs (negative = silent lead-in). Playback is paused. */
	void LoadSong(std::shared_ptr<const Amp::FSongAudio> Song, double StartMs);
	void Unload();
	void SetPlaying(bool bPlaying);
	void SetPlaybackRate(double Rate);
	/** Mutes or restores one instrument with a 200 ms crossfade (spec 10.3.2). */
	void SetLaneMuted(int32 Lane, bool bMuted);
	void SetMusicGain(float LinearGain);
	/** Debug: only the given lane is audible; INDEX_NONE restores the full mix. */
	void SetSoloLane(int32 Lane);

	/** Thread-safe copy of the latest play head snapshot. */
	FAmplitudeAudioPosition GetAudioPosition() const;

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	/** Only touched on the audio render thread (via synth commands). */
	Amp::FStemMixer Mixer;

	mutable FCriticalSection PositionLock;
	FAmplitudeAudioPosition Position;
	uint64 NextSerial = 1;
};
